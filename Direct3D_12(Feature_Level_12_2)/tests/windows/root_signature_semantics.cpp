#define INITGUID
#include <windows.h>

#include <d3d12.h>

#include <cstdio>
#include <cstring>

using CreateDeviceFn = HRESULT(WINAPI *)(
    IUnknown *, D3D_FEATURE_LEVEL, REFIID, void **);
using SerializeRootFn = HRESULT(WINAPI *)(
    const D3D12_ROOT_SIGNATURE_DESC *, D3D_ROOT_SIGNATURE_VERSION,
    ID3DBlob **, ID3DBlob **);
using SerializeVersionedRootFn = HRESULT(WINAPI *)(
    const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *, ID3DBlob **, ID3DBlob **);
using CreateRootDeserializerFn = HRESULT(WINAPI *)(
    const void *, SIZE_T, REFIID, void **);
using CreateVersionedRootDeserializerFn = HRESULT(WINAPI *)(
    const void *, SIZE_T, REFIID, void **);
using CompilerAbiFn = UINT(WINAPI *)();

template <typename T>
class ComObject {
public:
    ~ComObject()
    {
        if (value_ != nullptr)
            value_->Release();
    }

    T *get() const { return value_; }
    T **put()
    {
        if (value_ != nullptr)
            value_->Release();
        value_ = nullptr;
        return &value_;
    }
    T *operator->() const { return value_; }
    explicit operator bool() const { return value_ != nullptr; }

private:
    T *value_ = nullptr;
};

template <typename Function>
static Function
load_function(HMODULE module, const char *name)
{
    FARPROC raw = GetProcAddress(module, name);
    Function function = nullptr;
    static_assert(sizeof(function) == sizeof(raw));
    std::memcpy(&function, &raw, sizeof(function));
    return function;
}

static bool
expect_result(const char *name, HRESULT actual, HRESULT expected)
{
    std::printf("%s=0x%08lx\n", name, static_cast<unsigned long>(actual));
    if (actual == expected)
        return true;
    std::fprintf(
        stderr, "%s expected 0x%08lx, got 0x%08lx\n", name,
        static_cast<unsigned long>(expected),
        static_cast<unsigned long>(actual));
    return false;
}

static int
run()
{
    HMODULE winemetal = LoadLibraryA("winemetal.dll");
    HMODULE d3d12 = LoadLibraryA("d3d12.dll");
    if (winemetal == nullptr || d3d12 == nullptr)
        return 10;

    auto create_device = load_function<CreateDeviceFn>(
        d3d12, "D3D12CreateDevice");
    auto serialize_root = load_function<SerializeRootFn>(
        d3d12, "D3D12SerializeRootSignature");
    auto serialize_versioned = load_function<SerializeVersionedRootFn>(
        d3d12, "D3D12SerializeVersionedRootSignature");
    auto create_deserializer = load_function<CreateRootDeserializerFn>(
        d3d12, "D3D12CreateRootSignatureDeserializer");
    auto create_versioned_deserializer =
        load_function<CreateVersionedRootDeserializerFn>(
            d3d12, "D3D12CreateVersionedRootSignatureDeserializer");
    auto compiler_abi = load_function<CompilerAbiFn>(
        d3d12, "ADX12GetCompilerABI");
    if (create_device == nullptr || serialize_root == nullptr ||
        serialize_versioned == nullptr || create_deserializer == nullptr ||
        create_versioned_deserializer == nullptr || compiler_abi == nullptr)
        return 11;

    std::printf("adx12_compiler_abi=%u\n", compiler_abi());
    if (compiler_abi() != 2)
        return 12;

    ComObject<ID3D12Device> device;
    HRESULT hr = create_device(
        nullptr, D3D_FEATURE_LEVEL_11_0, IID_ID3D12Device,
        reinterpret_cast<void **>(device.put()));
    if (!expect_result("create_device", hr, S_OK))
        return 20;

    D3D12_ROOT_SIGNATURE_DESC empty = {};
    ComObject<ID3DBlob> empty_blob;
    ComObject<ID3DBlob> error_blob;
    hr = serialize_root(
        &empty, D3D_ROOT_SIGNATURE_VERSION_1, empty_blob.put(),
        error_blob.put());
    if (!expect_result("serialize_empty", hr, S_OK))
        return 21;

    ComObject<ID3D12RootSignatureDeserializer> empty_deserializer;
    hr = create_deserializer(
        empty_blob->GetBufferPointer(), empty_blob->GetBufferSize(),
        IID_ID3D12RootSignatureDeserializer,
        reinterpret_cast<void **>(empty_deserializer.put()));
    if (!expect_result("deserialize_empty", hr, S_OK) ||
        empty_deserializer->GetRootSignatureDesc()->NumParameters != 0)
        return 22;

    D3D12_ROOT_PARAMETER1 uav = {};
    uav.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    uav.Descriptor.ShaderRegister = 7;
    uav.Descriptor.RegisterSpace = 3;
    uav.Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE;
    uav.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC versioned = {};
    versioned.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    versioned.Desc_1_1.NumParameters = 1;
    versioned.Desc_1_1.pParameters = &uav;
    ComObject<ID3DBlob> versioned_blob;
    hr = serialize_versioned(
        &versioned, versioned_blob.put(), error_blob.put());
    if (!expect_result("serialize_versioned", hr, S_OK))
        return 23;

    ComObject<ID3D12VersionedRootSignatureDeserializer> versioned_deserializer;
    hr = create_versioned_deserializer(
        versioned_blob->GetBufferPointer(), versioned_blob->GetBufferSize(),
        IID_ID3D12VersionedRootSignatureDeserializer,
        reinterpret_cast<void **>(versioned_deserializer.put()));
    if (!expect_result("deserialize_versioned", hr, S_OK))
        return 24;
    const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *decoded =
        versioned_deserializer->GetUnconvertedRootSignatureDesc();
    if (decoded == nullptr || decoded->Version != D3D_ROOT_SIGNATURE_VERSION_1_1 ||
        decoded->Desc_1_1.NumParameters != 1 ||
        decoded->Desc_1_1.pParameters[0].ParameterType !=
            D3D12_ROOT_PARAMETER_TYPE_UAV ||
        decoded->Desc_1_1.pParameters[0].Descriptor.ShaderRegister != 7 ||
        decoded->Desc_1_1.pParameters[0].Descriptor.RegisterSpace != 3 ||
        decoded->Desc_1_1.pParameters[0].Descriptor.Flags !=
            D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE)
        return 25;
    std::printf("versioned_roundtrip=passed\n");

    D3D12_DESCRIPTOR_RANGE mixed_ranges[2] = {};
    mixed_ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    mixed_ranges[0].NumDescriptors = 1;
    mixed_ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    mixed_ranges[1].NumDescriptors = 1;
    D3D12_ROOT_PARAMETER mixed_parameter = {};
    mixed_parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    mixed_parameter.DescriptorTable.NumDescriptorRanges = 2;
    mixed_parameter.DescriptorTable.pDescriptorRanges = mixed_ranges;
    D3D12_ROOT_SIGNATURE_DESC mixed_desc = {};
    mixed_desc.NumParameters = 1;
    mixed_desc.pParameters = &mixed_parameter;
    ComObject<ID3DBlob> mixed_blob;
    hr = serialize_root(
        &mixed_desc, D3D_ROOT_SIGNATURE_VERSION_1, mixed_blob.put(),
        error_blob.put());
    if (!expect_result("serialize_mixed_table", hr, S_OK))
        return 26;
    ComObject<ID3D12RootSignature> root_signature;
    hr = device->CreateRootSignature(
        0, mixed_blob->GetBufferPointer(), mixed_blob->GetBufferSize(),
        IID_ID3D12RootSignature,
        reinterpret_cast<void **>(root_signature.put()));
    if (!expect_result("reject_mixed_table", hr, E_INVALIDARG))
        return 27;

    D3D12_ROOT_PARAMETER constants = {};
    constants.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    constants.Constants.Num32BitValues = D3D12_MAX_ROOT_COST;
    D3D12_ROOT_SIGNATURE_DESC limit_desc = {};
    limit_desc.NumParameters = 1;
    limit_desc.pParameters = &constants;
    ComObject<ID3DBlob> limit_blob;
    hr = serialize_root(
        &limit_desc, D3D_ROOT_SIGNATURE_VERSION_1, limit_blob.put(),
        error_blob.put());
    if (!expect_result("serialize_64_dwords", hr, S_OK))
        return 28;
    hr = device->CreateRootSignature(
        0, limit_blob->GetBufferPointer(), limit_blob->GetBufferSize(),
        IID_ID3D12RootSignature,
        reinterpret_cast<void **>(root_signature.put()));
    if (!expect_result("admit_64_dwords", hr, S_OK))
        return 29;

    constants.Constants.Num32BitValues = D3D12_MAX_ROOT_COST + 1;
    hr = serialize_root(
        &limit_desc, D3D_ROOT_SIGNATURE_VERSION_1, limit_blob.put(),
        error_blob.put());
    if (!expect_result("serialize_65_dwords", hr, S_OK))
        return 30;
    hr = device->CreateRootSignature(
        0, limit_blob->GetBufferPointer(), limit_blob->GetBufferSize(),
        IID_ID3D12RootSignature,
        reinterpret_cast<void **>(root_signature.put()));
    if (!expect_result("reject_65_dwords", hr, E_INVALIDARG))
        return 31;

    std::printf("root_signature_semantics=passed\n");
    return 0;
}

int
main()
{
    return run();
}
