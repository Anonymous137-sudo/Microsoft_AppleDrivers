#define INITGUID
#include <windows.h>
#include <tlhelp32.h>

#include <d3d12.h>
#include <dxgi1_6.h>

#include <cstdio>
#include <cstring>
#include <cwctype>

using CreateDXGIFactory2Fn = HRESULT(WINAPI *)(UINT, REFIID, void **);
using D3D12CreateDeviceFn = HRESULT(WINAPI *)(
    IUnknown *, D3D_FEATURE_LEVEL, REFIID, void **);
using ADX12GetCompilerABIFn = UINT(WINAPI *)();

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
get_module_path(HMODULE module, char *path, size_t capacity)
{
    DWORD length = GetModuleFileNameA(module, path, static_cast<DWORD>(capacity));
    return length > 0 && length < capacity;
}

static bool
path_is_beneath_root(const char *path, const char *root)
{
    size_t path_length = std::strlen(path);
    size_t root_length = std::strlen(root);

    while (root_length > 0 &&
           (root[root_length - 1] == '\\' || root[root_length - 1] == '/'))
        root_length--;
    if (path_length <= root_length ||
        (path[root_length] != '\\' && path[root_length] != '/'))
        return false;
    return _strnicmp(path, root, root_length) == 0;
}

static bool
module_has_forbidden_renderer(void)
{
    static const wchar_t *const forbidden[] = {
        L"wined3d",
        L"winevulkan",
        L"dxvk",
        L"vkd3d",
        L"d3dmetal",
    };
    HANDLE snapshot = CreateToolhelp32Snapshot(
        TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
    if (snapshot == INVALID_HANDLE_VALUE)
        return true;

    MODULEENTRY32W module = {};
    module.dwSize = sizeof(module);
    bool forbidden_loaded = false;
    if (Module32FirstW(snapshot, &module)) {
        do {
            wchar_t lowered[MAX_PATH] = {};
            size_t length = wcsnlen(module.szExePath, MAX_PATH - 1);
            for (size_t i = 0; i < length; i++)
                lowered[i] = static_cast<wchar_t>(towlower(module.szExePath[i]));
            for (const wchar_t *needle : forbidden) {
                if (wcsstr(lowered, needle) != nullptr) {
                    std::printf("forbidden_renderer=%ls\n", module.szExePath);
                    forbidden_loaded = true;
                    break;
                }
            }
        } while (!forbidden_loaded && Module32NextW(snapshot, &module));
    }
    CloseHandle(snapshot);
    return forbidden_loaded;
}

int
main()
{
    // Admit WineMetal's builtin/Unix bridge before resolving native DXGI imports.
    HMODULE winemetal_module = LoadLibraryA("winemetal.dll");
    if (winemetal_module == nullptr) {
        std::printf("winemetal_load_error=%lu\n", GetLastError());
        return 10;
    }

    HMODULE dxgi_module = LoadLibraryA("dxgi.dll");
    if (dxgi_module == nullptr) {
        std::printf("dxgi_load_error=%lu\n", GetLastError());
        FreeLibrary(winemetal_module);
        return 10;
    }

    HMODULE d3d12_module = LoadLibraryA("d3d12.dll");
    if (d3d12_module == nullptr) {
        std::printf("d3d12_load_error=%lu\n", GetLastError());
        FreeLibrary(dxgi_module);
        FreeLibrary(winemetal_module);
        return 10;
    }

    char dxgi_path[4096] = {};
    char d3d12_path[4096] = {};
    char winemetal_path[4096] = {};
    char expected_root[4096] = {};
    DWORD expected_length = GetEnvironmentVariableA(
        "ADX12_EXPECTED_MODULE_ROOT", expected_root, sizeof(expected_root));
    if (!get_module_path(dxgi_module, dxgi_path, sizeof(dxgi_path)) ||
        !get_module_path(d3d12_module, d3d12_path, sizeof(d3d12_path)) ||
        !get_module_path(winemetal_module, winemetal_path, sizeof(winemetal_path)) ||
        expected_length == 0 || expected_length >= sizeof(expected_root)) {
        std::printf("module_provenance=unavailable\n");
        return 12;
    }
    std::printf("dxgi_module=%s\n", dxgi_path);
    std::printf("d3d12_module=%s\n", d3d12_path);
    std::printf("winemetal_module=%s\n", winemetal_path);
    if (!path_is_beneath_root(dxgi_path, expected_root) ||
        !path_is_beneath_root(d3d12_path, expected_root) ||
        !path_is_beneath_root(winemetal_path, expected_root)) {
        std::printf("module_provenance=mismatch\n");
        return 12;
    }
    std::printf("module_provenance=adx12-runtime\n");
    if (module_has_forbidden_renderer())
        return 13;

    auto create_factory = load_function<CreateDXGIFactory2Fn>(
        dxgi_module, "CreateDXGIFactory2");
    auto create_device = load_function<D3D12CreateDeviceFn>(
        d3d12_module, "D3D12CreateDevice");
    auto compiler_abi = load_function<ADX12GetCompilerABIFn>(
        d3d12_module, "ADX12GetCompilerABI");
    if (create_factory == nullptr || create_device == nullptr ||
        compiler_abi == nullptr) {
        std::printf("export_error=%lu\n", GetLastError());
        return 11;
    }
    UINT compiler_abi_version = compiler_abi();
    std::printf("adx12_compiler_abi=%u\n", compiler_abi_version);
    if (compiler_abi_version != 2)
        return 14;

    IDXGIFactory4 *factory = nullptr;
    HRESULT result = create_factory(
        0, IID_IDXGIFactory4, reinterpret_cast<void **>(&factory));
    std::printf("create_factory=0x%08lx\n", static_cast<unsigned long>(result));
    if (FAILED(result))
        return 20;

    IDXGIFactory6 *factory6 = nullptr;
    result = factory->QueryInterface(
        IID_IDXGIFactory6, reinterpret_cast<void **>(&factory6));
    std::printf("query_factory6=0x%08lx\n", static_cast<unsigned long>(result));
    if (FAILED(result)) {
        factory->Release();
        return 22;
    }

    IUnknown *factory_identity = nullptr;
    IUnknown *factory6_identity = nullptr;
    result = factory->QueryInterface(
        IID_IUnknown, reinterpret_cast<void **>(&factory_identity));
    if (SUCCEEDED(result))
        result = factory6->QueryInterface(
            IID_IUnknown, reinterpret_cast<void **>(&factory6_identity));
    bool factory_identity_matches =
        SUCCEEDED(result) && factory_identity == factory6_identity;
    std::printf("factory_identity=%s\n",
                factory_identity_matches ? "stable" : "mismatch");
    if (factory_identity != nullptr)
        factory_identity->Release();
    if (factory6_identity != nullptr)
        factory6_identity->Release();
    factory6->Release();
    if (!factory_identity_matches) {
        factory->Release();
        return 23;
    }

    IDXGIAdapter1 *adapter = nullptr;
    result = factory->EnumAdapters1(0, &adapter);
    std::printf("enum_adapter=0x%08lx\n", static_cast<unsigned long>(result));
    if (FAILED(result)) {
        factory->Release();
        return 21;
    }

    IDXGIAdapter4 *adapter4 = nullptr;
    result = adapter->QueryInterface(
        IID_IDXGIAdapter4, reinterpret_cast<void **>(&adapter4));
    std::printf("query_adapter4=0x%08lx\n", static_cast<unsigned long>(result));
    if (FAILED(result)) {
        adapter->Release();
        factory->Release();
        return 24;
    }

    IDXGIFactory4 *adapter_parent = nullptr;
    result = adapter4->GetParent(
        IID_IDXGIFactory4, reinterpret_cast<void **>(&adapter_parent));
    std::printf("adapter_parent=0x%08lx\n", static_cast<unsigned long>(result));
    if (adapter_parent != nullptr)
        adapter_parent->Release();
    adapter4->Release();
    if (FAILED(result)) {
        adapter->Release();
        factory->Release();
        return 25;
    }

    DXGI_ADAPTER_DESC1 adapter_desc = {};
    result = adapter->GetDesc1(&adapter_desc);
    std::printf("adapter_desc=0x%08lx\n", static_cast<unsigned long>(result));
    if (SUCCEEDED(result)) {
        std::printf("adapter_vendor=0x%04x\n", adapter_desc.VendorId);
        std::printf("adapter_device=0x%04x\n", adapter_desc.DeviceId);
        std::printf("adapter_flags=0x%08x\n", adapter_desc.Flags);
    }

    ID3D12Device *device = nullptr;
    result = create_device(
        adapter,
        D3D_FEATURE_LEVEL_11_0,
        IID_ID3D12Device,
        reinterpret_cast<void **>(&device));
    std::printf("create_device=0x%08lx\n", static_cast<unsigned long>(result));
    if (FAILED(result)) {
        adapter->Release();
        factory->Release();
        return 30;
    }

    ID3D12Device1 *device1 = nullptr;
    result = device->QueryInterface(
        IID_ID3D12Device1, reinterpret_cast<void **>(&device1));
    std::printf("query_device1=0x%08lx\n", static_cast<unsigned long>(result));
    if (device1 != nullptr)
        device1->Release();
    if (FAILED(result)) {
        device->Release();
        adapter->Release();
        factory->Release();
        return 32;
    }

    D3D_FEATURE_LEVEL requested_levels[] = {
        static_cast<D3D_FEATURE_LEVEL>(0xc200),
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0,
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };
    D3D12_FEATURE_DATA_FEATURE_LEVELS feature_levels = {
        static_cast<UINT>(sizeof(requested_levels) / sizeof(requested_levels[0])),
        requested_levels,
        D3D_FEATURE_LEVEL_11_0,
    };
    result = device->CheckFeatureSupport(
        D3D12_FEATURE_FEATURE_LEVELS,
        &feature_levels,
        sizeof(feature_levels));
    std::printf("feature_levels=0x%08lx\n", static_cast<unsigned long>(result));
    if (SUCCEEDED(result))
        std::printf("maximum_feature_level=0x%04x\n",
                    static_cast<unsigned int>(feature_levels.MaxSupportedFeatureLevel));

    D3D12_COMMAND_QUEUE_DESC queue_desc = {};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queue_desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    ID3D12CommandQueue *queue = nullptr;
    result = device->CreateCommandQueue(
        &queue_desc,
        IID_ID3D12CommandQueue,
        reinterpret_cast<void **>(&queue));
    std::printf("create_queue=0x%08lx\n", static_cast<unsigned long>(result));

    if (SUCCEEDED(result)) {
        ID3D12Device *queue_device = nullptr;
        result = queue->GetDevice(
            IID_ID3D12Device, reinterpret_cast<void **>(&queue_device));
        std::printf("queue_get_device=0x%08lx\n",
                    static_cast<unsigned long>(result));
        if (queue_device != nullptr)
            queue_device->Release();
    }

    if (queue != nullptr)
        queue->Release();
    device->Release();
    adapter->Release();
    factory->Release();
    FreeLibrary(d3d12_module);
    FreeLibrary(dxgi_module);
    FreeLibrary(winemetal_module);
    return FAILED(result) ? 31 : 0;
}
