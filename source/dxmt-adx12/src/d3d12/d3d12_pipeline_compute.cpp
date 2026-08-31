/*
 * Copyright 2026 Feifan He for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "Metal.hpp"
#include "com/com_pointer.hpp"
#include "d3d12_device.hpp"
#include "d3d12_pageable.hpp"
#include "log/log.hpp"
#include "airconv_public.h"
#include "sha256.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace dxmt {

namespace {

enum class KkBundleStatus {
  NotRequested,
  Loaded,
  Invalid,
};

struct KkComputeBundle {
  std::string source;
  std::string entrypoint;
  WMTSize threadgroup_size;
  uint32_t root_set0_offset;
  uint32_t root_size;
  uint32_t descriptor_count;
  D3D12_ROOT_PARAMETER_TYPE descriptor_types[16];
  uint32_t descriptor_dword_counts[16] = {};
};

std::string
get_process_environment(const char *name) {
  DWORD required = GetEnvironmentVariableA(name, nullptr, 0);
  if (required == 0)
    return {};

  std::vector<char> buffer(required);
  DWORD copied = GetEnvironmentVariableA(name, buffer.data(), required);
  if (copied == 0 || copied >= required)
    return {};
  return std::string(buffer.data(), copied);
}

std::string
sha256_string(const void *data, size_t size) {
  static constexpr char hex[] = "0123456789abcdef";
  auto hash = compute_sha256_hash(static_cast<const uint8_t *>(data), size);
  std::string result(sizeof(hash.hash) * 2, '0');
  for (size_t i = 0; i < sizeof(hash.hash); i++) {
    result[i * 2] = hex[hash.hash[i] >> 4];
    result[i * 2 + 1] = hex[hash.hash[i] & 0xf];
  }
  return result;
}

bool
read_text_file(const char *path, std::string &contents) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input)
    return false;

  auto end = input.tellg();
  constexpr std::streamoff max_source_size = 16 * 1024 * 1024;
  if (end <= 0 || end > max_source_size)
    return false;
  contents.resize(static_cast<size_t>(end));
  input.seekg(0, std::ios::beg);
  return static_cast<bool>(input.read(contents.data(), static_cast<std::streamsize>(end)));
}

bool
parse_u32(const char *text, uint32_t &value) {
  if (text == nullptr || *text == '\0')
    return false;
  char *end = nullptr;
  unsigned long parsed = std::strtoul(text, &end, 10);
  if (end == text || *end != '\0' || parsed > std::numeric_limits<uint32_t>::max())
    return false;
  value = static_cast<uint32_t>(parsed);
  return true;
}

bool
parse_threadgroup(const char *text, WMTSize &size) {
  unsigned int x = 0, y = 0, z = 0;
  int consumed = 0;
  if (text == nullptr || std::sscanf(text, "%u,%u,%u%n", &x, &y, &z, &consumed) != 3 || text[consumed] != '\0')
    return false;
  if (x == 0 || y == 0 || z == 0)
    return false;
  size = {x, y, z};
  return true;
}

bool
parse_manifest(const std::string &contents, std::unordered_map<std::string, std::string> &values) {
  size_t offset = 0;
  while (offset < contents.size()) {
    auto end = contents.find('\n', offset);
    if (end == std::string::npos)
      end = contents.size();
    auto line = contents.substr(offset, end - offset);
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    offset = end + 1;
    if (line.empty() || line[0] == '#')
      continue;

    auto separator = line.find('=');
    if (separator == std::string::npos || separator == 0 || separator + 1 >= line.size())
      return false;
    auto key = line.substr(0, separator);
    auto value = line.substr(separator + 1);
    if (key.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") != std::string::npos ||
        value.find_first_of("\r\n") != std::string::npos || !values.emplace(key, value).second)
      return false;
  }
  return true;
}

bool
is_hex_sha256(const std::string &value) {
  return value.size() == 64 &&
         value.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos;
}

bool
is_relative_filename(const std::string &value) {
  return !value.empty() && value != "." && value != ".." &&
         value.find_first_of("/\\:") == std::string::npos;
}

std::string
manifest_sibling(const std::string &manifest, const std::string &filename) {
  auto separator = manifest.find_last_of("/\\");
  if (separator == std::string::npos)
    return filename;
  return manifest.substr(0, separator + 1) + filename;
}

KkBundleStatus
load_kk_compute_bundle(const D3D12_COMPUTE_PIPELINE_STATE_DESC *desc, KkComputeBundle &bundle) {
  auto manifest_path = get_process_environment("ADX12_COMPILER_MANIFEST");
  if (manifest_path.empty())
    return KkBundleStatus::NotRequested;
  std::fprintf(stderr, "ADX12_COMPILER_CACHE=requested\n");

  std::string manifest_source;
  std::unordered_map<std::string, std::string> values;
  if (!read_text_file(manifest_path.c_str(), manifest_source) ||
      !parse_manifest(manifest_source, values)) {
    ERR("ADX12 compiler manifest is unavailable or malformed");
    return KkBundleStatus::Invalid;
  }

  auto value = [&](const char *key) -> const std::string & {
    static const std::string empty;
    auto entry = values.find(key);
    return entry == values.end() ? empty : entry->second;
  };
  auto &schema = value("schema");
  auto &compiler_abi = value("compiler_abi");
  auto &cache_key = value("cache_key");
  auto &expected_dxil = value("dxil_sha256");
  auto &expected_msl = value("msl_sha256");
  auto &msl_file = value("msl_file");
  auto &stage = value("stage");
  auto &entrypoint = value("entry_point");
  auto &threadgroup = value("threadgroup_size");
  auto &root_set0_offset = value("kk_root_set0_offset");
  auto &root_size = value("kk_root_size");
  auto &descriptor_count = value("descriptor_count");
  if (schema != "adx12-compiler-cache-v1" || compiler_abi != "2" ||
      !is_hex_sha256(cache_key) || !is_hex_sha256(expected_dxil) ||
      !is_hex_sha256(expected_msl) || !is_relative_filename(msl_file) ||
      stage != "compute" || entrypoint.empty() ||
      !parse_u32(descriptor_count.c_str(), bundle.descriptor_count) ||
      bundle.descriptor_count < 1 || bundle.descriptor_count > 16 ||
      !parse_threadgroup(threadgroup.c_str(), bundle.threadgroup_size) ||
      !parse_u32(root_set0_offset.c_str(), bundle.root_set0_offset) ||
      !parse_u32(root_size.c_str(), bundle.root_size)) {
    ERR("ADX12 compiler manifest has an incompatible schema or reflection contract");
    return KkBundleStatus::Invalid;
  }

  for (uint32_t i = 0; i < bundle.descriptor_count; ++i) {
    auto key = "descriptor_" + std::to_string(i);
    auto suffix = ",parameter" + std::to_string(i);
    auto &descriptor = value(key.c_str());
    const std::string binding_prefix = "set0,binding";
    auto binding_end = descriptor.find(",storage-buffer,");
    auto uniform_binding_end = descriptor.find(",uniform-buffer,");
    if (binding_end == std::string::npos)
      binding_end = uniform_binding_end;
    uint32_t binding = 0;
    bool valid_binding = binding_end != std::string::npos &&
                         descriptor.compare(0, binding_prefix.size(), binding_prefix) == 0 &&
                         parse_u32(descriptor.substr(binding_prefix.size(), binding_end - binding_prefix.size()).c_str(), binding);
    auto read_only = ",storage-buffer,read-only,root-srv" + suffix;
    auto write_only = ",storage-buffer,write-only,root-uav" + suffix;
    auto uniform = ",uniform-buffer,read-only,root-cbv" + suffix;
    const std::string root_constants_prefix = ",uniform-buffer,read-only,root-constants,dwords";
    if (valid_binding && descriptor.substr(binding_end) == read_only) {
      bundle.descriptor_types[i] = D3D12_ROOT_PARAMETER_TYPE_SRV;
    } else if (valid_binding && descriptor.substr(binding_end) == write_only) {
      bundle.descriptor_types[i] = D3D12_ROOT_PARAMETER_TYPE_UAV;
    } else if (valid_binding && descriptor.substr(binding_end) == uniform) {
      bundle.descriptor_types[i] = D3D12_ROOT_PARAMETER_TYPE_CBV;
    } else if (valid_binding && descriptor.compare(binding_end, root_constants_prefix.size(), root_constants_prefix) == 0) {
      auto count_begin = binding_end + root_constants_prefix.size();
      auto count_end = descriptor.find(suffix, count_begin);
      if (count_end == std::string::npos || count_end + suffix.size() != descriptor.size() ||
          !parse_u32(descriptor.substr(count_begin, count_end - count_begin).c_str(), bundle.descriptor_dword_counts[i]) ||
          bundle.descriptor_dword_counts[i] == 0 || bundle.descriptor_dword_counts[i] > D3D12_MAX_ROOT_COST) {
        ERR("ADX12 root-constant reflection has an invalid DWORD count");
        return KkBundleStatus::Invalid;
      }
      bundle.descriptor_types[i] = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    } else {
      ERR("ADX12 compiler manifest descriptor reflection is incompatible");
      return KkBundleStatus::Invalid;
    }
  }

  if ((bundle.root_set0_offset & 7) || (bundle.root_size & 7) ||
      bundle.root_size < bundle.root_set0_offset + sizeof(uint64_t) ||
      bundle.root_size > 64 * 1024) {
    ERR("ADX12 KK root descriptor layout is invalid");
    return KkBundleStatus::Invalid;
  }

  if (_stricmp(sha256_string(desc->CS.pShaderBytecode, desc->CS.BytecodeLength).c_str(), expected_dxil.c_str()) != 0) {
    ERR("ADX12 compiler cache entry does not match the requested DXIL");
    return KkBundleStatus::Invalid;
  }
  auto source_path = manifest_sibling(manifest_path, msl_file);
  if (!read_text_file(source_path.c_str(), bundle.source) ||
      _stricmp(sha256_string(bundle.source.data(), bundle.source.size()).c_str(), expected_msl.c_str()) != 0) {
    ERR("ADX12 compiler cache MSL is unavailable or failed hash validation");
    return KkBundleStatus::Invalid;
  }

  bundle.entrypoint = entrypoint;
  return KkBundleStatus::Loaded;
}

} // namespace

class MTLD3D12ComputePipelineStateImpl : public MTLD3D12Pageable<MTLD3D12ComputePipelineState> {

  sm50_shader_t shader_cs;
  MTL_SHADER_REFLECTION ref_cs;

public:
  MTLD3D12ComputePipelineStateImpl(MTLD3D12Device *pDevice) : MTLD3D12Pageable<MTLD3D12ComputePipelineState>(pDevice) {
    IsComputePipelineState = 1;
  }

  HRESULT
  Initialize(const D3D12_COMPUTE_PIPELINE_STATE_DESC *pDesc) {

    auto metal = device_->GetMTLDevice();
    WMT::Reference<WMT::Error> err;
    KkComputeBundle kk_bundle;
    auto kk_status = load_kk_compute_bundle(pDesc, kk_bundle);
    if (kk_status == KkBundleStatus::Invalid)
      return E_INVALIDARG;
    if (kk_status == KkBundleStatus::Loaded) {
      auto root_signature = static_cast<MTLD3D12RootSignature *>(pDesc->pRootSignature);
      if (root_signature == nullptr || root_signature->ParameterSlots != kk_bundle.descriptor_count) {
        ERR("ADX12 KK compute root signature does not match reflected descriptor count");
        return E_INVALIDARG;
      }
      for (uint32_t i = 0; i < kk_bundle.descriptor_count; ++i) {
        if (root_signature->ParameterTypes[i] != kk_bundle.descriptor_types[i]) {
          ERR("ADX12 KK compute root descriptor type does not match reflection");
          return E_INVALIDARG;
        }
        if (kk_bundle.descriptor_types[i] == D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS &&
            root_signature->ParameterDwordCounts[i] != kk_bundle.descriptor_dword_counts[i]) {
          ERR("ADX12 KK compute root-constant count does not match reflection");
          return E_INVALIDARG;
        }
      }

      auto cs_lib = metal.newLibraryWithSource(kk_bundle.source, err);
      if (!cs_lib) {
        ERR("Failed to compile ADX12 KK compute source: ", err.description().getUTF8String());
        return E_FAIL;
      }
      auto cs_func = cs_lib.newFunction(kk_bundle.entrypoint.c_str());
      if (!cs_func) {
        ERR("ADX12 KK compute entry point is unavailable");
        return E_FAIL;
      }

      WMTComputePipelineInfo info;
      WMT::InitializeComputePipelineInfo(info);
      info.compute_function = cs_func;
      info.support_indirect_command_buffers = true;
      pso = metal.newComputePipelineState(info, err);
      if (!pso) {
        ERR("Failed to create ADX12 KK compute PSO: ", err.description().getUTF8String());
        return E_FAIL;
      }

      threadgroup_size = kk_bundle.threadgroup_size;
      UsesKkDescriptorAbi = 1;
      KkDescriptorCount = kk_bundle.descriptor_count;
      std::copy_n(kk_bundle.descriptor_types, kk_bundle.descriptor_count, KkDescriptorTypes);
      std::copy_n(kk_bundle.descriptor_dword_counts, kk_bundle.descriptor_count, KkDescriptorDwordCounts);
      KkRootSet0Offset = kk_bundle.root_set0_offset;
      KkRootSize = kk_bundle.root_size;
      Logger::info("ADX12_COMPILER_CACHE=admitted");
      std::fprintf(stderr, "ADX12_COMPILER_CACHE=admitted\n");
      return S_OK;
    }

    sm50_error_t sm50_err;

    SM50_SHADER_ROOT_SIGNATURE_DATA rootsig;
    rootsig.type = SM50_SHADER_ROOT_SIGNATURE;
    if (pDesc->pRootSignature) {
      rootsig.bytecode_length = static_cast<MTLD3D12RootSignature *>(pDesc->pRootSignature)->GetBlob(&rootsig.bytecode);
    } else {
      rootsig.bytecode = pDesc->CS.pShaderBytecode;
      rootsig.bytecode_length = pDesc->CS.BytecodeLength;
    }
    rootsig.next = nullptr;

    SM50_SHADER_COMMON_DATA common;
    common.flags = {};
    common.type = SM50_SHADER_COMMON;
    common.metal_version = SM50_SHADER_METAL_310;
    common.next = &rootsig;

    if (SM50Initialize(pDesc->CS.pShaderBytecode, pDesc->CS.BytecodeLength, &shader_cs, &ref_cs, &sm50_err)) {
      ERR("Failed to parse cs shader");
      return E_FAIL;
    }

    threadgroup_size = {ref_cs.ThreadgroupSize[0], ref_cs.ThreadgroupSize[1], ref_cs.ThreadgroupSize[2]};

    sm50_bitcode_t cs_bitcode;

    if (SM50Compile(shader_cs, (SM50_SHADER_COMPILATION_ARGUMENT_DATA *)&common, "cs_main", &cs_bitcode, &sm50_err)) {
      ERR("Failed to compile cs shader");
      return E_FAIL;
    }

    SM50_COMPILED_BITCODE cs_bitcode_compiled;

    SM50GetCompiledBitcode(cs_bitcode, &cs_bitcode_compiled);

    auto cs_data = WMT::MakeDispatchData(cs_bitcode_compiled.Data, cs_bitcode_compiled.Size);

    auto cs_lib = metal.newLibrary(cs_data, err);

    auto cs_func = cs_lib.newFunction("cs_main");

    // PSO
    {
      WMTComputePipelineInfo info;
      WMT::InitializeComputePipelineInfo(info);
      info.compute_function = cs_func;
      info.support_indirect_command_buffers = true;

      pso = metal.newComputePipelineState(info, err);
      if (!pso) {
        ERR("Failed to create compute PSO: ", err.description().getUTF8String());
        return E_FAIL;
      }
    }

    return S_OK;
  }

  HRESULT
  STDMETHODCALLTYPE
  QueryInterface(REFIID riid, void **ppvObject) {
    if (ppvObject == nullptr)
      return E_POINTER;

    *ppvObject = nullptr;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D12Object) || riid == __uuidof(ID3D12DeviceChild) ||
        riid == __uuidof(ID3D12Pageable) || riid == __uuidof(ID3D12PipelineState)) {
      *ppvObject = ref(this);
      return S_OK;
    }

    if (logQueryInterfaceError(__uuidof(ID3D12PipelineState), riid)) {
      WARN("D3D12ComputePipelineState: Unknown interface query ", str::format(riid));
    }

    return E_NOINTERFACE;
  }

  virtual HRESULT STDMETHODCALLTYPE
  GetCachedBlob(ID3DBlob **blob) {
    IMPLEMENT_ME
    return E_NOTIMPL;
  }
};

HRESULT
CreateComputePipelineState(
    MTLD3D12Device *pDevice, const D3D12_COMPUTE_PIPELINE_STATE_DESC *pDesc, REFIID riid, void **ppPipelineState
) {
  auto pso = Com(new MTLD3D12ComputePipelineStateImpl(pDevice));
  HRESULT hr = pso->Initialize(pDesc);
  if (FAILED(hr))
    return hr;
  return pso->QueryInterface(riid, ppPipelineState);
};

} // namespace dxmt
