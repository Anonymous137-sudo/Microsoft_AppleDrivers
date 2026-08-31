#include "adx12/frontend_environment.h"

#include <stdio.h>

int
main(void)
{
    ADX12FrontendEnvironment environment = {
        .struct_size = sizeof(environment),
    };
    ADX12FrontendStatus status =
        adx12_frontend_inspect_environment(&environment);

    printf("status=%s\n", adx12_frontend_status_string(status));
    printf("host=%s\n", adx12_frontend_host_string(environment.host));
    printf("wine_prefix=%s\n", environment.has_wine_prefix ? "yes" : "no");
    printf("crossover_bottle=%s\n",
           environment.has_crossover_bottle ? "yes" : "no");
    printf("d3d12_native=%s\n",
           environment.native_d3d12_override ? "yes" : "no");
    printf("dxgi_native=%s\n",
           environment.native_dxgi_override ? "yes" : "no");
    printf("winemetal_builtin=%s\n",
           environment.builtin_winemetal_override ? "yes" : "no");
    printf("renderer_conflict=%s\n",
           environment.renderer_conflict ? environment.renderer_name : "none");
    return status == ADX12_FRONTEND_OK ? 0 : 1;
}
