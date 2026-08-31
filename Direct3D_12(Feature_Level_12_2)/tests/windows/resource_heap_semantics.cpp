#define INITGUID
#include <windows.h>
#include <d3d12.h>

#include <cstdio>
#include <cstdint>
#include <cstring>

using CreateDeviceFn = HRESULT(WINAPI *)(IUnknown *, D3D_FEATURE_LEVEL, REFIID, void **);

static D3D12_RESOURCE_DESC buffer_desc(UINT64 width)
{
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = width;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return desc;
}

static D3D12_RESOURCE_DESC texture_desc(UINT width, UINT height)
{
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    return desc;
}

static bool expect(const char *name, bool passed)
{
    std::printf("%s=%s\n", name, passed ? "passed" : "failed");
    return passed;
}

static bool test_texture_alias_handoff(
    ID3D12Device *device, ID3D12Resource *before, ID3D12Resource *after)
{
    constexpr UINT width = 64;
    constexpr UINT height = 64;
    constexpr UINT row_pitch = 256;
    constexpr UINT64 byte_count = UINT64(row_pitch) * height;
    bool passed = before && after;
    HRESULT hr = S_OK;

    D3D12_HEAP_PROPERTIES upload_props = {};
    upload_props.Type = D3D12_HEAP_TYPE_UPLOAD;
    upload_props.CreationNodeMask = upload_props.VisibleNodeMask = 1;
    D3D12_HEAP_PROPERTIES readback_props = upload_props;
    readback_props.Type = D3D12_HEAP_TYPE_READBACK;
    auto staging_desc = buffer_desc(byte_count);
    ID3D12Resource *upload = nullptr;
    ID3D12Resource *readback = nullptr;
    if (passed) {
        hr = device->CreateCommittedResource(&upload_props, D3D12_HEAP_FLAG_NONE,
            &staging_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_ID3D12Resource, reinterpret_cast<void **>(&upload));
        passed &= SUCCEEDED(hr) && upload;
    }
    if (passed) {
        hr = device->CreateCommittedResource(&readback_props, D3D12_HEAP_FLAG_NONE,
            &staging_desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_ID3D12Resource, reinterpret_cast<void **>(&readback));
        passed &= SUCCEEDED(hr) && readback;
    }

    void *mapped = nullptr;
    D3D12_RANGE no_read = {0, 0};
    if (passed) {
        hr = upload->Map(0, &no_read, &mapped);
        passed &= SUCCEEDED(hr) && mapped;
    }
    if (mapped) {
        auto pixels = static_cast<std::uint32_t *>(mapped);
        for (UINT i = 0; i < width * height; ++i)
            pixels[i] = 0xff000000u | ((i * 0x1021u) & 0x00ffffffu);
        D3D12_RANGE written = {0, static_cast<SIZE_T>(byte_count)};
        upload->Unmap(0, &written);
    }

    ID3D12CommandQueue *queue = nullptr;
    ID3D12CommandAllocator *allocator = nullptr;
    ID3D12GraphicsCommandList *list = nullptr;
    ID3D12Fence *fence = nullptr;
    if (passed) {
        D3D12_COMMAND_QUEUE_DESC queue_desc = {};
        queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        hr = device->CreateCommandQueue(&queue_desc, IID_ID3D12CommandQueue,
            reinterpret_cast<void **>(&queue));
        passed &= SUCCEEDED(hr) && queue;
    }
    if (passed) {
        hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_ID3D12CommandAllocator, reinterpret_cast<void **>(&allocator));
        passed &= SUCCEEDED(hr) && allocator;
    }
    if (passed) {
        hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            allocator, nullptr, IID_ID3D12GraphicsCommandList,
            reinterpret_cast<void **>(&list));
        passed &= SUCCEEDED(hr) && list;
    }
    if (passed) {
        D3D12_TEXTURE_COPY_LOCATION upload_location = {};
        upload_location.pResource = upload;
        upload_location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        upload_location.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        upload_location.PlacedFootprint.Footprint.Width = width;
        upload_location.PlacedFootprint.Footprint.Height = height;
        upload_location.PlacedFootprint.Footprint.Depth = 1;
        upload_location.PlacedFootprint.Footprint.RowPitch = row_pitch;
        D3D12_TEXTURE_COPY_LOCATION before_location = {};
        before_location.pResource = before;
        before_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        list->CopyTextureRegion(&before_location, 0, 0, 0, &upload_location, nullptr);

        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
        barrier.Aliasing.pResourceBefore = before;
        barrier.Aliasing.pResourceAfter = after;
        list->ResourceBarrier(1, &barrier);

        D3D12_TEXTURE_COPY_LOCATION after_location = before_location;
        after_location.pResource = after;
        D3D12_TEXTURE_COPY_LOCATION readback_location = upload_location;
        readback_location.pResource = readback;
        list->CopyTextureRegion(&readback_location, 0, 0, 0, &after_location, nullptr);
        hr = list->Close();
        passed &= SUCCEEDED(hr);
    }
    if (passed) {
        ID3D12CommandList *lists[] = {list};
        queue->ExecuteCommandLists(1, lists);
        hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_ID3D12Fence,
            reinterpret_cast<void **>(&fence));
        passed &= SUCCEEDED(hr) && fence;
    }
    if (passed) {
        HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        hr = queue->Signal(fence, 1);
        if (SUCCEEDED(hr) && event) {
            hr = fence->SetEventOnCompletion(1, event);
            passed &= SUCCEEDED(hr) &&
                WaitForSingleObject(event, 30000) == WAIT_OBJECT_0;
            CloseHandle(event);
        } else {
            passed = false;
        }
    }
    mapped = nullptr;
    if (passed) {
        D3D12_RANGE read_range = {0, static_cast<SIZE_T>(byte_count)};
        hr = readback->Map(0, &read_range, &mapped);
        passed &= SUCCEEDED(hr) && mapped;
    }
    if (mapped) {
        auto pixels = static_cast<const std::uint32_t *>(mapped);
        for (UINT i = 0; i < width * height; ++i)
            passed &= pixels[i] == (0xff000000u | ((i * 0x1021u) & 0x00ffffffu));
        D3D12_RANGE no_write = {0, 0};
        readback->Unmap(0, &no_write);
    }

    if (fence) fence->Release();
    if (list) list->Release();
    if (allocator) allocator->Release();
    if (queue) queue->Release();
    if (readback) readback->Release();
    if (upload) upload->Release();
    return passed;
}

int main()
{
    HMODULE module = LoadLibraryA("d3d12.dll");
    FARPROC raw_create_device = module ?
        GetProcAddress(module, "D3D12CreateDevice") : nullptr;
    CreateDeviceFn create_device = nullptr;
    static_assert(sizeof(create_device) == sizeof(raw_create_device));
    std::memcpy(&create_device, &raw_create_device, sizeof(create_device));
    ID3D12Device *device = nullptr;
    if (!create_device || FAILED(create_device(nullptr, D3D_FEATURE_LEVEL_11_0,
            IID_ID3D12Device, reinterpret_cast<void **>(&device))))
        return 10;

    bool passed = true;
    D3D12_RESOURCE_DESC descs[2] = {buffer_desc(1), buffer_desc(65537)};
    auto single = device->GetResourceAllocationInfo(0, 1, &descs[0]);
    passed &= expect("single_alignment", single.Alignment == D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
    passed &= expect("single_size", single.SizeInBytes == D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
    auto pair = device->GetResourceAllocationInfo(0, 2, descs);
    passed &= expect("pair_alignment", pair.Alignment == D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
    passed &= expect("pair_size", pair.SizeInBytes == 3 * D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);

    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap.CreationNodeMask = heap.VisibleNodeMask = 1;
    struct Case { const char *name; D3D12_RESOURCE_DESC desc; } cases[] = {
        {"reject_zero_width", buffer_desc(0)},
        {"reject_buffer_height", buffer_desc(32)},
        {"reject_buffer_format", buffer_desc(32)},
        {"reject_buffer_layout", buffer_desc(32)},
    };
    cases[1].desc.Height = 2;
    cases[2].desc.Format = DXGI_FORMAT_R32_UINT;
    cases[3].desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    for (const auto &test : cases) {
        ID3D12Resource *resource = reinterpret_cast<ID3D12Resource *>(1);
        HRESULT hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
            &test.desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
            IID_ID3D12Resource, reinterpret_cast<void **>(&resource));
        passed &= expect(test.name, hr == E_INVALIDARG && resource == nullptr);
        if (SUCCEEDED(hr) && resource)
            resource->Release();
    }

    D3D12_HEAP_DESC heap_desc = {};
    heap_desc.SizeInBytes = 2 * D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    heap_desc.Properties = heap;
    heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
    ID3D12Heap *placed_heap = nullptr;
    HRESULT hr = device->CreateHeap(
        &heap_desc, IID_ID3D12Heap, reinterpret_cast<void **>(&placed_heap));
    passed &= expect("create_buffer_heap", SUCCEEDED(hr) && placed_heap != nullptr);
    D3D12_RESOURCE_DESC placed_desc = buffer_desc(32);
    if (placed_heap) {
        ID3D12Resource *placed = nullptr;
        hr = device->CreatePlacedResource(placed_heap,
            D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT, &placed_desc,
            D3D12_RESOURCE_STATE_COMMON, nullptr, IID_ID3D12Resource,
            reinterpret_cast<void **>(&placed));
        passed &= expect("create_placed_buffer", SUCCEEDED(hr) && placed != nullptr);

        ID3D12Resource *alias_before = nullptr;
        ID3D12Resource *alias_after = nullptr;
        hr = device->CreatePlacedResource(placed_heap, 0, &placed_desc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_ID3D12Resource,
            reinterpret_cast<void **>(&alias_before));
        bool alias_handoff = SUCCEEDED(hr) && alias_before != nullptr;
        hr = device->CreatePlacedResource(placed_heap, 0, &placed_desc,
            D3D12_RESOURCE_STATE_COPY_SOURCE, nullptr, IID_ID3D12Resource,
            reinterpret_cast<void **>(&alias_after));
        alias_handoff &= SUCCEEDED(hr) && alias_after != nullptr;

        D3D12_HEAP_PROPERTIES upload_props = {};
        upload_props.Type = D3D12_HEAP_TYPE_UPLOAD;
        upload_props.CreationNodeMask = upload_props.VisibleNodeMask = 1;
        D3D12_HEAP_PROPERTIES readback_props = upload_props;
        readback_props.Type = D3D12_HEAP_TYPE_READBACK;
        ID3D12Resource *upload = nullptr;
        ID3D12Resource *readback = nullptr;
        if (alias_handoff) {
            hr = device->CreateCommittedResource(&upload_props, D3D12_HEAP_FLAG_NONE,
                &placed_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                IID_ID3D12Resource, reinterpret_cast<void **>(&upload));
            alias_handoff &= SUCCEEDED(hr) && upload != nullptr;
        }
        if (alias_handoff) {
            hr = device->CreateCommittedResource(&readback_props, D3D12_HEAP_FLAG_NONE,
                &placed_desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                IID_ID3D12Resource, reinterpret_cast<void **>(&readback));
            alias_handoff &= SUCCEEDED(hr) && readback != nullptr;
        }

        const std::uint32_t pattern[8] = {
            0x10203040u, 0x50607080u, 0x90a0b0c0u, 0xd0e0f001u,
            0x12345678u, 0x89abcdefu, 0x0badc0deu, 0xc001d00du};
        void *mapped = nullptr;
        D3D12_RANGE no_read = {0, 0};
        if (alias_handoff) {
            hr = upload->Map(0, &no_read, &mapped);
            alias_handoff &= SUCCEEDED(hr) && mapped != nullptr;
        }
        if (mapped) {
            std::memcpy(mapped, pattern, sizeof(pattern));
            D3D12_RANGE written = {0, sizeof(pattern)};
            upload->Unmap(0, &written);
        }

        ID3D12CommandQueue *queue = nullptr;
        ID3D12CommandAllocator *allocator = nullptr;
        ID3D12GraphicsCommandList *list = nullptr;
        ID3D12Fence *fence = nullptr;
        if (alias_handoff) {
            D3D12_COMMAND_QUEUE_DESC queue_desc = {};
            queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
            hr = device->CreateCommandQueue(&queue_desc, IID_ID3D12CommandQueue,
                reinterpret_cast<void **>(&queue));
            alias_handoff &= SUCCEEDED(hr) && queue != nullptr;
        }
        if (alias_handoff) {
            hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_ID3D12CommandAllocator, reinterpret_cast<void **>(&allocator));
            alias_handoff &= SUCCEEDED(hr) && allocator != nullptr;
        }
        if (alias_handoff) {
            hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                allocator, nullptr, IID_ID3D12GraphicsCommandList,
                reinterpret_cast<void **>(&list));
            alias_handoff &= SUCCEEDED(hr) && list != nullptr;
        }
        if (alias_handoff) {
            list->CopyBufferRegion(alias_before, 0, upload, 0, sizeof(pattern));
            D3D12_RESOURCE_BARRIER alias_barrier = {};
            alias_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
            alias_barrier.Aliasing.pResourceBefore = alias_before;
            alias_barrier.Aliasing.pResourceAfter = alias_after;
            list->ResourceBarrier(1, &alias_barrier);
            list->CopyBufferRegion(readback, 0, alias_after, 0, sizeof(pattern));
            hr = list->Close();
            alias_handoff &= SUCCEEDED(hr);
        }
        if (alias_handoff) {
            ID3D12CommandList *lists[] = {list};
            queue->ExecuteCommandLists(1, lists);
            hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_ID3D12Fence,
                reinterpret_cast<void **>(&fence));
            alias_handoff &= SUCCEEDED(hr) && fence != nullptr;
        }
        if (alias_handoff) {
            hr = queue->Signal(fence, 1);
            HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (SUCCEEDED(hr) && event) {
                hr = fence->SetEventOnCompletion(1, event);
                alias_handoff &= SUCCEEDED(hr) &&
                    WaitForSingleObject(event, 30000) == WAIT_OBJECT_0;
                CloseHandle(event);
            } else {
                alias_handoff = false;
            }
        }
        mapped = nullptr;
        if (alias_handoff) {
            D3D12_RANGE read_range = {0, sizeof(pattern)};
            hr = readback->Map(0, &read_range, &mapped);
            alias_handoff &= SUCCEEDED(hr) && mapped != nullptr &&
                std::memcmp(mapped, pattern, sizeof(pattern)) == 0;
            if (mapped) {
                D3D12_RANGE no_write = {0, 0};
                readback->Unmap(0, &no_write);
            }
        }
        passed &= expect("placed_buffer_alias_handoff", alias_handoff);
        if (fence) fence->Release();
        if (list) list->Release();
        if (allocator) allocator->Release();
        if (queue) queue->Release();
        if (readback) readback->Release();
        if (upload) upload->Release();
        if (alias_after) alias_after->Release();
        if (alias_before) alias_before->Release();
        if (placed) placed->Release();

        placed = reinterpret_cast<ID3D12Resource *>(1);
        hr = device->CreatePlacedResource(placed_heap, 1, &placed_desc,
            D3D12_RESOURCE_STATE_COMMON, nullptr, IID_ID3D12Resource,
            reinterpret_cast<void **>(&placed));
        passed &= expect("reject_unaligned_offset", hr == E_INVALIDARG && placed == nullptr);

        placed_desc.Width = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT + 1;
        placed = reinterpret_cast<ID3D12Resource *>(1);
        hr = device->CreatePlacedResource(placed_heap,
            D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT, &placed_desc,
            D3D12_RESOURCE_STATE_COMMON, nullptr, IID_ID3D12Resource,
            reinterpret_cast<void **>(&placed));
        passed &= expect("reject_placed_overflow", hr == E_INVALIDARG && placed == nullptr);
        placed_heap->Release();
    }

    heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;
    D3D12_RESOURCE_DESC placed_texture_desc = texture_desc(64, 64);
    auto texture_info = device->GetResourceAllocationInfo(0, 1, &placed_texture_desc);
    passed &= expect("texture_allocation_alignment",
        texture_info.Alignment >= D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
    passed &= expect("texture_allocation_size",
        texture_info.SizeInBytes != 0 && texture_info.SizeInBytes != UINT64_MAX &&
        texture_info.SizeInBytes % texture_info.Alignment == 0);
    heap_desc.SizeInBytes = 2 * texture_info.SizeInBytes;
    placed_heap = nullptr;
    hr = device->CreateHeap(
        &heap_desc, IID_ID3D12Heap, reinterpret_cast<void **>(&placed_heap));
    if (placed_heap) {
        placed_desc = buffer_desc(32);
        ID3D12Resource *placed = reinterpret_cast<ID3D12Resource *>(1);
        hr = device->CreatePlacedResource(placed_heap, 0, &placed_desc,
            D3D12_RESOURCE_STATE_COMMON, nullptr, IID_ID3D12Resource,
            reinterpret_cast<void **>(&placed));
        passed &= expect("reject_buffer_on_texture_heap", hr == E_INVALIDARG && placed == nullptr);

        placed = nullptr;
        hr = device->CreatePlacedResource(placed_heap, 0, &placed_texture_desc,
            D3D12_RESOURCE_STATE_COMMON, nullptr, IID_ID3D12Resource,
            reinterpret_cast<void **>(&placed));
        passed &= expect("create_placed_texture", SUCCEEDED(hr) && placed != nullptr);

        ID3D12Resource *alias = nullptr;
        hr = device->CreatePlacedResource(placed_heap, 0, &placed_texture_desc,
            D3D12_RESOURCE_STATE_COMMON, nullptr, IID_ID3D12Resource,
            reinterpret_cast<void **>(&alias));
        passed &= expect("create_aliased_texture", SUCCEEDED(hr) && alias != nullptr);
        passed &= expect("placed_texture_alias_handoff",
            test_texture_alias_handoff(device, placed, alias));

        ID3D12Resource *second = nullptr;
        hr = device->CreatePlacedResource(placed_heap, texture_info.SizeInBytes,
            &placed_texture_desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
            IID_ID3D12Resource, reinterpret_cast<void **>(&second));
        passed &= expect("create_offset_texture", SUCCEEDED(hr) && second != nullptr);

        ID3D12Resource *unaligned = reinterpret_cast<ID3D12Resource *>(1);
        hr = device->CreatePlacedResource(placed_heap, 1, &placed_texture_desc,
            D3D12_RESOURCE_STATE_COMMON, nullptr, IID_ID3D12Resource,
            reinterpret_cast<void **>(&unaligned));
        passed &= expect("reject_unaligned_texture", hr == E_INVALIDARG && unaligned == nullptr);

        ID3D12Resource *overflow = reinterpret_cast<ID3D12Resource *>(1);
        hr = device->CreatePlacedResource(placed_heap, 2 * texture_info.SizeInBytes,
            &placed_texture_desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
            IID_ID3D12Resource, reinterpret_cast<void **>(&overflow));
        passed &= expect("reject_placed_texture_overflow", hr == E_INVALIDARG && overflow == nullptr);
        placed_heap->Release();

        D3D12_RESOURCE_DESC retained_desc = {};
        if (placed)
            placed->GetDesc(&retained_desc);
        passed &= expect("placed_texture_retains_heap",
            placed && retained_desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D);
        if (second)
            second->Release();
        if (alias)
            alias->Release();
        if (placed)
            placed->Release();
    } else {
        passed &= expect("reject_buffer_on_texture_heap", false);
    }

    heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
    heap_desc.SizeInBytes = 2 * texture_info.SizeInBytes;
    placed_heap = nullptr;
    hr = device->CreateHeap(
        &heap_desc, IID_ID3D12Heap, reinterpret_cast<void **>(&placed_heap));
    if (placed_heap) {
        ID3D12Resource *placed = reinterpret_cast<ID3D12Resource *>(1);
        hr = device->CreatePlacedResource(placed_heap, 0, &placed_texture_desc,
            D3D12_RESOURCE_STATE_COMMON, nullptr, IID_ID3D12Resource,
            reinterpret_cast<void **>(&placed));
        passed &= expect("reject_texture_on_buffer_heap", hr == E_INVALIDARG && placed == nullptr);
        placed_heap->Release();
    } else {
        passed &= expect("reject_texture_on_buffer_heap", false);
    }

    device->Release();
    FreeLibrary(module);
    std::printf("resource_heap_semantics=%s\n", passed ? "passed" : "failed");
    return passed ? 0 : 1;
}
