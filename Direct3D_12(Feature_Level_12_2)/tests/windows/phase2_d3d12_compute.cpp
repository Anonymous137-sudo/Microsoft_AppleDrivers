#define INITGUID
#include <windows.h>

#include <d3d12.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using D3D12CreateDeviceFn = HRESULT(WINAPI *)(
    IUnknown *, D3D_FEATURE_LEVEL, REFIID, void **);
using D3D12SerializeRootSignatureFn = HRESULT(WINAPI *)(
    const D3D12_ROOT_SIGNATURE_DESC *, D3D_ROOT_SIGNATURE_VERSION,
    ID3DBlob **, ID3DBlob **);
using ADX12GetCompilerABIFn = UINT(WINAPI *)();

template <typename T>
class ComObject {
public:
    ComObject() = default;
    ComObject(const ComObject &) = delete;
    ComObject &operator=(const ComObject &) = delete;

    ~ComObject()
    {
        if (value_ != nullptr)
            value_->Release();
    }

    T *get() const { return value_; }
    T **put()
    {
        if (value_ != nullptr) {
            value_->Release();
            value_ = nullptr;
        }
        return &value_;
    }
    T *operator->() const { return value_; }
    explicit operator bool() const { return value_ != nullptr; }

private:
    T *value_ = nullptr;
};

class Module {
public:
    explicit Module(const char *name) : value_(LoadLibraryA(name)) {}
    Module(const Module &) = delete;
    Module &operator=(const Module &) = delete;

    ~Module()
    {
        if (value_ != nullptr)
            FreeLibrary(value_);
    }

    HMODULE get() const { return value_; }
    explicit operator bool() const { return value_ != nullptr; }

private:
    HMODULE value_ = nullptr;
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
read_file(const char *path, std::vector<std::uint8_t> *bytes)
{
    HANDLE file = CreateFileA(
        path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;

    LARGE_INTEGER size = {};
    bool success = GetFileSizeEx(file, &size) && size.QuadPart > 0 &&
        static_cast<unsigned long long>(size.QuadPart) <= MAXDWORD;
    if (success) {
        bytes->resize(static_cast<std::size_t>(size.QuadPart));
        DWORD bytes_read = 0;
        success = ReadFile(
            file, bytes->data(), static_cast<DWORD>(bytes->size()),
            &bytes_read, nullptr) && bytes_read == bytes->size();
    }
    CloseHandle(file);
    return success;
}

static void
print_blob(const char *label, ID3DBlob *blob)
{
    if (blob == nullptr || blob->GetBufferPointer() == nullptr)
        return;
    std::fprintf(
        stderr, "%s: %.*s\n", label,
        static_cast<int>(blob->GetBufferSize()),
        static_cast<const char *>(blob->GetBufferPointer()));
}

static D3D12_RESOURCE_DESC
buffer_desc(UINT64 size, D3D12_RESOURCE_FLAGS flags)
{
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = flags;
    return desc;
}

static D3D12_HEAP_PROPERTIES
heap_properties(D3D12_HEAP_TYPE type)
{
    D3D12_HEAP_PROPERTIES properties = {};
    properties.Type = type;
    properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    properties.CreationNodeMask = 1;
    properties.VisibleNodeMask = 1;
    return properties;
}

static bool
check_result(const char *operation, HRESULT result)
{
    std::printf(
        "%s=0x%08lx\n", operation, static_cast<unsigned long>(result));
    return SUCCEEDED(result);
}

static int
run(const char *dxil_path)
{
    std::vector<std::uint8_t> dxil;
    if (!read_file(dxil_path, &dxil)) {
        std::fprintf(stderr, "failed to read DXIL: %s\n", dxil_path);
        return 10;
    }

    Module winemetal("winemetal.dll");
    Module d3d12("d3d12.dll");
    if (!winemetal || !d3d12) {
        std::printf("module_load_error=%lu\n", GetLastError());
        return 11;
    }

    auto create_device = load_function<D3D12CreateDeviceFn>(
        d3d12.get(), "D3D12CreateDevice");
    auto serialize_root = load_function<D3D12SerializeRootSignatureFn>(
        d3d12.get(), "D3D12SerializeRootSignature");
    auto compiler_abi = load_function<ADX12GetCompilerABIFn>(
        d3d12.get(), "ADX12GetCompilerABI");
    if (create_device == nullptr || serialize_root == nullptr ||
        compiler_abi == nullptr) {
        std::printf("export_load_error=%lu\n", GetLastError());
        return 12;
    }
    UINT compiler_abi_version = compiler_abi();
    std::printf("adx12_compiler_abi=%u\n", compiler_abi_version);
    if (compiler_abi_version != 2)
        return 13;

    ComObject<ID3D12Device> device;
    HRESULT result = create_device(
        nullptr, D3D_FEATURE_LEVEL_11_0, IID_ID3D12Device,
        reinterpret_cast<void **>(device.put()));
    if (!check_result("create_device", result)) {
        return 20;
    }

    D3D12_ROOT_PARAMETER root_parameters[4] = {};
    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    root_parameters[0].Descriptor.ShaderRegister = 0;
    for (UINT i = 1; i < 3; ++i) {
        root_parameters[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        root_parameters[i].Descriptor.ShaderRegister = i - 1;
        root_parameters[i].Descriptor.RegisterSpace = 0;
        root_parameters[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    root_parameters[0].Descriptor.RegisterSpace = 0;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    root_parameters[3].Constants.ShaderRegister = 0;
    root_parameters[3].Constants.RegisterSpace = 0;
    root_parameters[3].Constants.Num32BitValues = 4;
    root_parameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC root_desc = {};
    root_desc.NumParameters = 4;
    root_desc.pParameters = root_parameters;

    ComObject<ID3DBlob> root_blob;
    ComObject<ID3DBlob> root_error;
    result = serialize_root(
        &root_desc, D3D_ROOT_SIGNATURE_VERSION_1,
        root_blob.put(), root_error.put());
    if (!check_result("serialize_root_signature", result)) {
        print_blob("root signature", root_error.get());
        return 21;
    }

    ComObject<ID3D12RootSignature> root_signature;
    result = device->CreateRootSignature(
        0, root_blob->GetBufferPointer(), root_blob->GetBufferSize(),
        IID_ID3D12RootSignature,
        reinterpret_cast<void **>(root_signature.put()));
    if (!check_result("create_root_signature", result)) {
        return 22;
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_desc = {};
    pipeline_desc.pRootSignature = root_signature.get();
    pipeline_desc.CS.pShaderBytecode = dxil.data();
    pipeline_desc.CS.BytecodeLength = dxil.size();

    ComObject<ID3D12PipelineState> pipeline;
    result = device->CreateComputePipelineState(
        &pipeline_desc, IID_ID3D12PipelineState,
        reinterpret_cast<void **>(pipeline.put()));
    if (!check_result("create_compute_pipeline", result)) {
        return 23;
    }

    constexpr UINT64 output_size = 4 * sizeof(std::uint32_t);
    D3D12_RESOURCE_DESC input_desc = buffer_desc(
        output_size, D3D12_RESOURCE_FLAG_NONE);
    D3D12_HEAP_PROPERTIES upload_heap = heap_properties(
        D3D12_HEAP_TYPE_UPLOAD);
    ComObject<ID3D12Resource> input;
    result = device->CreateCommittedResource(
        &upload_heap, D3D12_HEAP_FLAG_NONE, &input_desc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_ID3D12Resource, reinterpret_cast<void **>(input.put()));
    if (!check_result("create_input", result))
        return 24;
    void *input_mapped = nullptr;
    D3D12_RANGE no_read = {0, 0};
    result = input->Map(0, &no_read, &input_mapped);
    if (!check_result("map_input", result) || input_mapped == nullptr)
        return 24;
    const std::uint32_t input_values[4] = {7, 10, 13, 16};
    std::memcpy(input_mapped, input_values, sizeof(input_values));
    D3D12_RANGE input_written = {0, sizeof(input_values)};
    input->Unmap(0, &input_written);

    D3D12_RESOURCE_DESC output_desc = buffer_desc(
        output_size, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    D3D12_HEAP_PROPERTIES default_heap = heap_properties(
        D3D12_HEAP_TYPE_DEFAULT);
    ComObject<ID3D12Resource> output;
    result = device->CreateCommittedResource(
        &default_heap, D3D12_HEAP_FLAG_NONE, &output_desc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
        IID_ID3D12Resource, reinterpret_cast<void **>(output.put()));
    if (!check_result("create_output", result)) {
        return 24;
    }
    ComObject<ID3D12Resource> output_b;
    result = device->CreateCommittedResource(
        &default_heap, D3D12_HEAP_FLAG_NONE, &output_desc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
        IID_ID3D12Resource, reinterpret_cast<void **>(output_b.put()));
    if (!check_result("create_output_b", result))
        return 24;

    D3D12_RESOURCE_DESC readback_desc = buffer_desc(
        output_size, D3D12_RESOURCE_FLAG_NONE);
    D3D12_HEAP_PROPERTIES readback_heap = heap_properties(
        D3D12_HEAP_TYPE_READBACK);
    ComObject<ID3D12Resource> readback;
    result = device->CreateCommittedResource(
        &readback_heap, D3D12_HEAP_FLAG_NONE, &readback_desc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_ID3D12Resource, reinterpret_cast<void **>(readback.put()));
    if (!check_result("create_readback", result)) {
        return 25;
    }
    ComObject<ID3D12Resource> readback_b;
    result = device->CreateCommittedResource(
        &readback_heap, D3D12_HEAP_FLAG_NONE, &readback_desc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_ID3D12Resource, reinterpret_cast<void **>(readback_b.put()));
    if (!check_result("create_readback_b", result))
        return 25;

    D3D12_COMMAND_QUEUE_DESC queue_desc = {};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queue_desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    ComObject<ID3D12CommandQueue> queue;
    result = device->CreateCommandQueue(
        &queue_desc, IID_ID3D12CommandQueue,
        reinterpret_cast<void **>(queue.put()));
    if (!check_result("create_queue", result)) {
        return 26;
    }

    ComObject<ID3D12CommandAllocator> allocator;
    result = device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT, IID_ID3D12CommandAllocator,
        reinterpret_cast<void **>(allocator.put()));
    if (!check_result("create_allocator", result)) {
        return 27;
    }

    ComObject<ID3D12GraphicsCommandList> command_list;
    result = device->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.get(), pipeline.get(),
        IID_ID3D12GraphicsCommandList,
        reinterpret_cast<void **>(command_list.put()));
    if (!check_result("create_command_list", result)) {
        return 28;
    }

    command_list->SetComputeRootSignature(root_signature.get());
    command_list->SetComputeRootShaderResourceView(
        0, input->GetGPUVirtualAddress());
    command_list->SetComputeRootUnorderedAccessView(
        1, output->GetGPUVirtualAddress());
    command_list->SetComputeRootUnorderedAccessView(
        2, output_b->GetGPUVirtualAddress());
    const std::uint32_t root_constants[4] = {5, 0, 0, 0};
    command_list->SetComputeRoot32BitConstants(3, 4, root_constants, 0);
    command_list->Dispatch(1, 1, 1);

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = output.get();
    command_list->ResourceBarrier(1, &barrier);

    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = output.get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    command_list->ResourceBarrier(1, &barrier);
    barrier.Transition.pResource = output_b.get();
    command_list->ResourceBarrier(1, &barrier);
    command_list->CopyBufferRegion(
        readback.get(), 0, output.get(), 0, output_size);
    command_list->CopyBufferRegion(
        readback_b.get(), 0, output_b.get(), 0, output_size);

    result = command_list->Close();
    if (!check_result("close_command_list", result)) {
        return 29;
    }

    ID3D12CommandList *lists[] = {command_list.get()};
    queue->ExecuteCommandLists(1, lists);

    ComObject<ID3D12Fence> fence;
    result = device->CreateFence(
        0, D3D12_FENCE_FLAG_NONE, IID_ID3D12Fence,
        reinterpret_cast<void **>(fence.put()));
    if (!check_result("create_fence", result)) {
        return 30;
    }
    result = queue->Signal(fence.get(), 1);
    if (!check_result("queue_signal", result)) {
        return 31;
    }

    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (event == nullptr) {
        std::printf("create_event_error=%lu\n", GetLastError());
        return 32;
    }
    result = fence->SetEventOnCompletion(1, event);
    DWORD wait_result = FAILED(result) ? WAIT_FAILED : WaitForSingleObject(event, 30000);
    CloseHandle(event);
    std::printf("fence_value=%llu\n", static_cast<unsigned long long>(
        fence->GetCompletedValue()));
    if (!check_result("fence_wait", result) || wait_result != WAIT_OBJECT_0) {
        std::printf("fence_wait_result=%lu\n", wait_result);
        return 33;
    }

    void *mapped = nullptr;
    D3D12_RANGE read_range = {0, static_cast<SIZE_T>(output_size)};
    result = readback->Map(0, &read_range, &mapped);
    if (!check_result("map_readback", result) || mapped == nullptr) {
        return 34;
    }
    const auto *values = static_cast<const std::uint32_t *>(mapped);
    std::uint32_t copy[4] = {values[0], values[1], values[2], values[3]};
    D3D12_RANGE written_range = {0, 0};
    readback->Unmap(0, &written_range);

    mapped = nullptr;
    result = readback_b->Map(0, &read_range, &mapped);
    if (!check_result("map_readback_b", result) || mapped == nullptr)
        return 34;
    const auto *values_b = static_cast<const std::uint32_t *>(mapped);
    std::uint32_t copy_b[4] = {
        values_b[0], values_b[1], values_b[2], values_b[3]};
    readback_b->Unmap(0, &written_range);

    std::printf(
        "readback=%u,%u,%u,%u\n", copy[0], copy[1], copy[2], copy[3]);
    std::printf(
        "readback_b=%u,%u,%u,%u\n",
        copy_b[0], copy_b[1], copy_b[2], copy_b[3]);
    bool matched = copy[0] == 12 && copy[1] == 15 &&
        copy[2] == 18 && copy[3] == 21 && copy_b[0] == 19 &&
        copy_b[1] == 25 && copy_b[2] == 31 && copy_b[3] == 37;
    std::printf("d3d12_compute=%s\n", matched ? "passed" : "failed");

    return matched ? 0 : 35;
}

int
main(int argc, char **argv)
{
    if (argc != 2) {
        std::fprintf(stderr, "usage: phase2-d3d12-compute.exe shader.dxil\n");
        return 64;
    }
    return run(argv[1]);
}
