#ifndef ADX12_FRONTEND_ENVIRONMENT_H
#define ADX12_FRONTEND_ENVIRONMENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ADX12_FRONTEND_ENVIRONMENT_ABI 2u
#define ADX12_RENDERER_NAME_CAPACITY 64u

typedef enum ADX12HostEnvironment {
    ADX12_HOST_UNKNOWN = 0,
    ADX12_HOST_WINE = 1,
    ADX12_HOST_CROSSOVER = 2,
} ADX12HostEnvironment;

typedef enum ADX12FrontendStatus {
    ADX12_FRONTEND_OK = 0,
    ADX12_FRONTEND_INVALID_ARGUMENT = 1,
    ADX12_FRONTEND_WINDOWS_HOST_REQUIRED = 2,
    ADX12_FRONTEND_PROJECT_DLL_OVERRIDE_REQUIRED = 3,
    ADX12_FRONTEND_RENDERER_CONFLICT = 4,
} ADX12FrontendStatus;

typedef struct ADX12FrontendEnvironment {
    uint32_t struct_size;
    uint32_t abi_version;
    ADX12HostEnvironment host;
    bool has_wine_prefix;
    bool has_crossover_bottle;
    bool native_d3d12_override;
    bool native_dxgi_override;
    bool builtin_winemetal_override;
    bool renderer_conflict;
    char renderer_name[ADX12_RENDERER_NAME_CAPACITY];
} ADX12FrontendEnvironment;

bool adx12_frontend_override_is_native_only(
    const char *override_list,
    const char *dll_name);

bool adx12_frontend_override_is_builtin_only(
    const char *override_list,
    const char *dll_name);

bool adx12_frontend_renderer_image_is_forbidden(const char *image_name);

ADX12FrontendStatus adx12_frontend_inspect_environment(
    ADX12FrontendEnvironment *environment);

const char *adx12_frontend_status_string(ADX12FrontendStatus status);
const char *adx12_frontend_host_string(ADX12HostEnvironment host);

#ifdef __cplusplus
}
#endif

#endif
