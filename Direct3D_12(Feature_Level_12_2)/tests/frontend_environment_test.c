#include "adx12/frontend_environment.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
test_overrides(void)
{
    assert(adx12_frontend_override_is_native_only(
        "d3d12,dxgi=n;winemetal=b", "d3d12"));
    assert(adx12_frontend_override_is_native_only(
        "d3d12,dxgi=n;winemetal=b", "dxgi"));
    assert(adx12_frontend_override_is_builtin_only(
        "d3d12,dxgi=n;winemetal=b", "winemetal"));
    assert(adx12_frontend_override_is_builtin_only(
        "D3D12,DXGI,WINEMETAL=builtin", "winemetal"));
    assert(!adx12_frontend_override_is_builtin_only(
        "d3d12=b,n;dxgi=b", "d3d12"));
    assert(!adx12_frontend_override_is_native_only(
        "d3d12=n,b;dxgi=n;winemetal=b", "d3d12"));
    assert(!adx12_frontend_override_is_builtin_only(
        "d3d12,dxgi=n;winemetal=n", "winemetal"));
    assert(!adx12_frontend_override_is_builtin_only(
        "d3d12,dxgi=b,n;winemetal=b", "dxgi"));
}

static void
test_renderer_names(void)
{
    assert(!adx12_frontend_renderer_image_is_forbidden(
        "/runtime/D3DMetal.framework/D3DMetal"));
    assert(adx12_frontend_renderer_image_is_forbidden(
        "/runtime/libvkd3d-proton.dylib"));
    assert(adx12_frontend_renderer_image_is_forbidden(
        "C:\\windows\\system32\\wined3d.dll"));
    assert(adx12_frontend_renderer_image_is_forbidden("dxvk_d3d12.dll"));
    assert(!adx12_frontend_renderer_image_is_forbidden(
        "/runtime/libd3dshared.dylib"));
    assert(!adx12_frontend_renderer_image_is_forbidden(
        "/runtime/libADX12Metal.dylib"));
}

static void
test_environment(void)
{
    ADX12FrontendEnvironment environment = {
        .struct_size = sizeof(environment),
    };
    ADX12FrontendStatus status;

    assert(setenv("CX_BOTTLE", "adx12-test", 1) == 0);
    assert(setenv("WINEPREFIX", "/machine-neutral/test-prefix", 1) == 0);
    assert(setenv(
        "WINEDLLOVERRIDES", "d3d12,dxgi=n;winemetal=b", 1) == 0);
    status = adx12_frontend_inspect_environment(&environment);
    assert(status == ADX12_FRONTEND_OK);
    assert(environment.host == ADX12_HOST_CROSSOVER);
    assert(environment.has_wine_prefix);
    assert(environment.has_crossover_bottle);
    assert(environment.native_d3d12_override);
    assert(environment.native_dxgi_override);
    assert(environment.builtin_winemetal_override);
    assert(!environment.renderer_conflict);
    assert(environment.abi_version == ADX12_FRONTEND_ENVIRONMENT_ABI);

    assert(setenv(
        "WINEDLLOVERRIDES", "d3d12=n,b;dxgi=n;winemetal=b", 1) == 0);
    environment.struct_size = sizeof(environment);
    status = adx12_frontend_inspect_environment(&environment);
    assert(status == ADX12_FRONTEND_PROJECT_DLL_OVERRIDE_REQUIRED);
}

int
main(void)
{
    test_overrides();
    test_renderer_names();
    test_environment();
    puts("ADX12 frontend environment tests passed");
    return 0;
}
