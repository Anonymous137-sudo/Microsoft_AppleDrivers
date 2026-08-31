#include <windows.h>

#include "winemetal.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

namespace {

constexpr std::size_t kMaxVertexBuffers = 32;
constexpr std::size_t kMaxAttributes = 32;
constexpr std::size_t kMaxViewports = 16;
constexpr std::size_t kMaxDescriptorSets = 32;
constexpr std::size_t kMaxDynamicBuffers = 64;
constexpr std::size_t kMaxPushSize = 256;

struct KkBufferAddress {
    std::uint64_t base_address;
    std::uint32_t size;
    std::uint32_t zero;
};

struct KkDrawRoot {
    std::uint32_t buffer_strides[kMaxVertexBuffers];
    std::uint64_t attribute_bases[kMaxAttributes];
    std::uint32_t attribute_clamps[kMaxAttributes];
    float blend_constant[4];
    float clip_z_coefficient;
    float viewport_z_ranges[kMaxViewports * 2];
    bool emulate_depth_clamp;
    bool emulate_viewport_z;
};

union KkRootStageState {
    KkDrawRoot draw;
    std::uint32_t base_group[3];
};

struct KkRootDescriptorTable {
    std::uint64_t address;
    KkRootStageState stage;
    std::uint8_t push[kMaxPushSize];
    std::uint64_t sets[kMaxDescriptorSets];
    KkBufferAddress dynamic_buffers[kMaxDynamicBuffers];
    std::uint8_t set_dynamic_buffer_start[kMaxDescriptorSets];
};

static_assert(sizeof(KkBufferAddress) == 16);
static_assert(offsetof(KkRootDescriptorTable, sets) == 928);
static_assert(sizeof(KkRootDescriptorTable) == 2240);

template <typename Function>
Function load_function(HMODULE module, const char *name)
{
    FARPROC raw = GetProcAddress(module, name);
    Function function = nullptr;
    static_assert(sizeof(function) == sizeof(raw));
    std::memcpy(&function, &raw, sizeof(function));
    if (function == nullptr)
        std::printf("missing_export=%s\n", name);
    return function;
}

struct WineMetalApi {
    decltype(&NSObject_release) release;
    decltype(&NSArray_object) array_object;
    decltype(&NSArray_count) array_count;
    decltype(&WMTCopyAllDevices) copy_all_devices;
    decltype(&NSAutoreleasePool_alloc_init) new_autorelease_pool;
    decltype(&MTLDevice_newCommandQueue) new_command_queue;
    decltype(&MTLDevice_newBuffer) new_buffer;
    decltype(&MTLDevice_newLibraryWithSource) new_library_with_source;
    decltype(&MTLLibrary_newFunction) new_function;
    decltype(&MTLDevice_newComputePipelineState) new_compute_pipeline;
    decltype(&MTLCommandQueue_commandBuffer) new_command_buffer;
    decltype(&MTLCommandBuffer_computeCommandEncoder) new_compute_encoder;
    decltype(&MTLComputeCommandEncoder_encodeCommands) encode_compute_commands;
    decltype(&MTLCommandEncoder_endEncoding) end_encoding;
    decltype(&MTLCommandBuffer_commit) commit;
    decltype(&MTLCommandBuffer_waitUntilCompleted) wait_until_completed;
    decltype(&MTLCommandBuffer_status) command_buffer_status;
    decltype(&MTLCommandBuffer_error) command_buffer_error;
    decltype(&NSObject_description) object_description;
    decltype(&NSString_getCString) string_get_c_string;
};

bool load_api(HMODULE module, WineMetalApi *api)
{
#define LOAD(member, symbol) api->member = load_function<decltype(api->member)>(module, #symbol)
    LOAD(release, NSObject_release);
    LOAD(array_object, NSArray_object);
    LOAD(array_count, NSArray_count);
    LOAD(copy_all_devices, WMTCopyAllDevices);
    LOAD(new_autorelease_pool, NSAutoreleasePool_alloc_init);
    LOAD(new_command_queue, MTLDevice_newCommandQueue);
    LOAD(new_buffer, MTLDevice_newBuffer);
    LOAD(new_library_with_source, MTLDevice_newLibraryWithSource);
    LOAD(new_function, MTLLibrary_newFunction);
    LOAD(new_compute_pipeline, MTLDevice_newComputePipelineState);
    LOAD(new_command_buffer, MTLCommandQueue_commandBuffer);
    LOAD(new_compute_encoder, MTLCommandBuffer_computeCommandEncoder);
    LOAD(encode_compute_commands, MTLComputeCommandEncoder_encodeCommands);
    LOAD(end_encoding, MTLCommandEncoder_endEncoding);
    LOAD(commit, MTLCommandBuffer_commit);
    LOAD(wait_until_completed, MTLCommandBuffer_waitUntilCompleted);
    LOAD(command_buffer_status, MTLCommandBuffer_status);
    LOAD(command_buffer_error, MTLCommandBuffer_error);
    LOAD(object_description, NSObject_description);
    LOAD(string_get_c_string, NSString_getCString);
#undef LOAD

    return api->release != nullptr && api->array_object != nullptr &&
           api->array_count != nullptr && api->copy_all_devices != nullptr &&
           api->new_autorelease_pool != nullptr &&
           api->new_command_queue != nullptr && api->new_buffer != nullptr &&
           api->new_library_with_source != nullptr &&
           api->new_function != nullptr && api->new_compute_pipeline != nullptr &&
           api->new_command_buffer != nullptr &&
           api->new_compute_encoder != nullptr &&
           api->encode_compute_commands != nullptr &&
           api->end_encoding != nullptr && api->commit != nullptr &&
           api->wait_until_completed != nullptr &&
           api->command_buffer_status != nullptr &&
           api->command_buffer_error != nullptr &&
           api->object_description != nullptr &&
           api->string_get_c_string != nullptr;
}

std::string read_file(const char *path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        return {};
    return std::string(
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>());
}

void print_error(const WineMetalApi &api, const char *label, obj_handle_t error)
{
    std::printf("%s=0x%llx\n", label,
                static_cast<unsigned long long>(error));
    if (error == NULL_OBJECT_HANDLE)
        return;

    obj_handle_t description = api.object_description(error);
    if (description == NULL_OBJECT_HANDLE)
        return;
    char text[4096] = {};
    if (api.string_get_c_string(
            description, text, sizeof(text), WMTUTF8StringEncoding))
        std::printf("%s_description=%s\n", label, text);
}

obj_handle_t make_shared_buffer(
    const WineMetalApi &api, obj_handle_t device, std::uint64_t size,
    WMTBufferInfo *info)
{
    *info = {};
    info->length = size;
    info->options = static_cast<WMTResourceOptions>(
        WMTResourceStorageModeShared | WMTResourceHazardTrackingModeTracked);
    return api.new_buffer(device, info);
}

void link_command(wmtcmd_base *from, wmtcmd_base *to)
{
    WMT_MEMPTR_SET(from->next, to);
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 2) {
        std::fprintf(stderr, "usage: phase2-winemetal-compute.exe shader.metal\n");
        return 64;
    }

    const std::string source = read_file(argv[1]);
    if (source.empty()) {
        std::printf("shader_source=unreadable\n");
        return 10;
    }

    HMODULE module = LoadLibraryA("winemetal.dll");
    if (module == nullptr) {
        std::printf("winemetal_load_error=%lu\n", GetLastError());
        return 11;
    }

    WineMetalApi api = {};
    if (!load_api(module, &api)) {
        FreeLibrary(module);
        return 12;
    }

    obj_handle_t pool = api.new_autorelease_pool();
    obj_handle_t devices = api.copy_all_devices();
    if (devices == NULL_OBJECT_HANDLE || api.array_count(devices) == 0) {
        std::printf("metal_device=unavailable\n");
        if (devices != NULL_OBJECT_HANDLE)
            api.release(devices);
        if (pool != NULL_OBJECT_HANDLE)
            api.release(pool);
        FreeLibrary(module);
        return 20;
    }

    const obj_handle_t device = api.array_object(devices, 0);
    obj_handle_t queue = api.new_command_queue(device, 8);
    if (queue == NULL_OBJECT_HANDLE) {
        std::printf("command_queue=unavailable\n");
        api.release(devices);
        api.release(pool);
        FreeLibrary(module);
        return 21;
    }

    WMTBufferInfo input_info = {};
    WMTBufferInfo output_info = {};
    WMTBufferInfo output_b_info = {};
    WMTBufferInfo constants_info = {};
    WMTBufferInfo descriptor_info = {};
    WMTBufferInfo root_info = {};
    WMTBufferInfo sampler_info = {};
    obj_handle_t input = make_shared_buffer(api, device, 16, &input_info);
    obj_handle_t output = make_shared_buffer(api, device, 16, &output_info);
    obj_handle_t output_b = make_shared_buffer(api, device, 16, &output_b_info);
    obj_handle_t constants = make_shared_buffer(api, device, 256, &constants_info);
    obj_handle_t descriptor = make_shared_buffer(
        api, device, 4 * sizeof(KkBufferAddress), &descriptor_info);
    obj_handle_t root = make_shared_buffer(
        api, device, sizeof(KkRootDescriptorTable), &root_info);
    obj_handle_t sampler_table = make_shared_buffer(api, device, 32768, &sampler_info);
    if (input == 0 || output == 0 || output_b == 0 || constants == 0 || descriptor == 0 ||
        root == 0 || sampler_table == 0 || input_info.memory.ptr == nullptr ||
        output_info.memory.ptr == nullptr || output_b_info.memory.ptr == nullptr ||
        constants_info.memory.ptr == nullptr ||
        descriptor_info.memory.ptr == nullptr ||
        root_info.memory.ptr == nullptr || sampler_info.memory.ptr == nullptr) {
        std::printf("buffer_allocation=failed\n");
        return 22;
    }

    const std::uint32_t expected[] = {7, 10, 13, 16};
    std::memcpy(input_info.memory.ptr, expected, sizeof(expected));
    std::memset(output_info.memory.ptr, 0xcd, output_info.length);
    std::memset(output_b_info.memory.ptr, 0xcd, output_b_info.length);
    std::memset(constants_info.memory.ptr, 0, constants_info.length);
    *static_cast<std::uint32_t *>(constants_info.memory.ptr) = 5;
    std::memset(descriptor_info.memory.ptr, 0, descriptor_info.length);
    std::memset(root_info.memory.ptr, 0, root_info.length);
    std::memset(sampler_info.memory.ptr, 0, sampler_info.length);

    auto *buffer_addresses = static_cast<KkBufferAddress *>(descriptor_info.memory.ptr);
    buffer_addresses[0] = {
        input_info.gpu_address, static_cast<std::uint32_t>(input_info.length), 0};
    buffer_addresses[1] = {
        output_info.gpu_address, static_cast<std::uint32_t>(output_info.length), 0};
    buffer_addresses[2] = {
        output_b_info.gpu_address, static_cast<std::uint32_t>(output_b_info.length), 0};
    buffer_addresses[3] = {
        constants_info.gpu_address, static_cast<std::uint32_t>(constants_info.length), 0};

    auto *root_table = static_cast<KkRootDescriptorTable *>(root_info.memory.ptr);
    root_table->address = root_info.gpu_address;
    root_table->sets[0] = descriptor_info.gpu_address;

    obj_handle_t compile_error = 0;
    obj_handle_t library = api.new_library_with_source(
        device, source.data(), source.size(), &compile_error);
    if (library == 0) {
        print_error(api, "metal_library_error", compile_error);
        return 30;
    }

    obj_handle_t function = api.new_function(library, "main_entrypoint");
    if (function == 0) {
        std::printf("metal_function=missing\n");
        return 31;
    }

    WMTComputePipelineInfo pipeline_info = {};
    pipeline_info.compute_function = function;
    obj_handle_t pipeline_error = 0;
    obj_handle_t pipeline = api.new_compute_pipeline(
        device, &pipeline_info, &pipeline_error);
    if (pipeline == 0) {
        print_error(api, "metal_pipeline_error", pipeline_error);
        return 32;
    }

    obj_handle_t command_buffer = api.new_command_buffer(queue);
    obj_handle_t encoder = command_buffer == 0
        ? 0
        : api.new_compute_encoder(command_buffer, false);
    if (command_buffer == 0 || encoder == 0) {
        std::printf("compute_encoder=unavailable\n");
        return 40;
    }

    wmtcmd_compute_setpso set_pipeline = {};
    wmtcmd_compute_setbuffer set_root = {};
    wmtcmd_compute_setbuffer set_samplers = {};
    wmtcmd_compute_useresource use_descriptor = {};
    wmtcmd_compute_useresource use_input = {};
    wmtcmd_compute_useresource use_output = {};
    wmtcmd_compute_useresource use_output_b = {};
    wmtcmd_compute_useresource use_constants = {};
    wmtcmd_compute_dispatch dispatch = {};

    set_pipeline.type = WMTComputeCommandSetPSO;
    set_pipeline.pso = pipeline;
    set_pipeline.threadgroup_size = {4, 1, 1};
    set_root.type = WMTComputeCommandSetBuffer;
    set_root.buffer = root;
    set_root.index = 0;
    set_samplers.type = WMTComputeCommandSetBuffer;
    set_samplers.buffer = sampler_table;
    set_samplers.index = 1;
    use_descriptor.type = WMTComputeCommandUseResource;
    use_descriptor.resource = descriptor;
    use_descriptor.usage = WMTResourceUsageRead;
    use_input.type = WMTComputeCommandUseResource;
    use_input.resource = input;
    use_input.usage = WMTResourceUsageRead;
    use_output.type = WMTComputeCommandUseResource;
    use_output.resource = output;
    use_output.usage = static_cast<WMTResourceUsage>(
        WMTResourceUsageRead | WMTResourceUsageWrite);
    use_output_b.type = WMTComputeCommandUseResource;
    use_output_b.resource = output_b;
    use_output_b.usage = static_cast<WMTResourceUsage>(
        WMTResourceUsageRead | WMTResourceUsageWrite);
    use_constants.type = WMTComputeCommandUseResource;
    use_constants.resource = constants;
    use_constants.usage = WMTResourceUsageRead;
    dispatch.type = WMTComputeCommandDispatch;
    dispatch.size = {1, 1, 1};

    link_command(
        reinterpret_cast<wmtcmd_base *>(&set_pipeline),
        reinterpret_cast<wmtcmd_base *>(&set_root));
    link_command(
        reinterpret_cast<wmtcmd_base *>(&set_root),
        reinterpret_cast<wmtcmd_base *>(&set_samplers));
    link_command(
        reinterpret_cast<wmtcmd_base *>(&set_samplers),
        reinterpret_cast<wmtcmd_base *>(&use_descriptor));
    link_command(
        reinterpret_cast<wmtcmd_base *>(&use_descriptor),
        reinterpret_cast<wmtcmd_base *>(&use_input));
    link_command(
        reinterpret_cast<wmtcmd_base *>(&use_input),
        reinterpret_cast<wmtcmd_base *>(&use_output));
    link_command(
        reinterpret_cast<wmtcmd_base *>(&use_output),
        reinterpret_cast<wmtcmd_base *>(&use_output_b));
    link_command(
        reinterpret_cast<wmtcmd_base *>(&use_output_b),
        reinterpret_cast<wmtcmd_base *>(&use_constants));
    link_command(
        reinterpret_cast<wmtcmd_base *>(&use_constants),
        reinterpret_cast<wmtcmd_base *>(&dispatch));

    api.encode_compute_commands(
        encoder, reinterpret_cast<const wmtcmd_base *>(&set_pipeline));
    api.end_encoding(encoder);
    api.commit(command_buffer);
    api.wait_until_completed(command_buffer);

    const WMTCommandBufferStatus status = api.command_buffer_status(command_buffer);
    std::printf("command_buffer_status=%llu\n",
                static_cast<unsigned long long>(status));
    if (status != WMTCommandBufferStatusCompleted) {
        print_error(api, "command_buffer_error", api.command_buffer_error(command_buffer));
        return 41;
    }

    const auto *values = static_cast<const std::uint32_t *>(output_info.memory.ptr);
    std::printf("readback=%u,%u,%u,%u\n",
                values[0], values[1], values[2], values[3]);
    const auto *values_b = static_cast<const std::uint32_t *>(output_b_info.memory.ptr);
    std::printf("readback_b=%u,%u,%u,%u\n",
                values_b[0], values_b[1], values_b[2], values_b[3]);
    const bool matched = values[0] == 12 && values[1] == 15 &&
                         values[2] == 18 && values[3] == 21 &&
                         values_b[0] == 19 && values_b[1] == 25 &&
                         values_b[2] == 31 && values_b[3] == 37;
    std::printf("phase2_compute=%s\n", matched ? "passed" : "failed");

    api.release(encoder);
    api.release(command_buffer);
    api.release(pipeline);
    api.release(function);
    api.release(library);
    if (pipeline_error != 0)
        api.release(pipeline_error);
    if (compile_error != 0)
        api.release(compile_error);
    api.release(sampler_table);
    api.release(root);
    api.release(descriptor);
    api.release(constants);
    api.release(output_b);
    api.release(output);
    api.release(input);
    api.release(queue);
    api.release(devices);
    api.release(pool);
    FreeLibrary(module);
    return matched ? 0 : 42;
}
