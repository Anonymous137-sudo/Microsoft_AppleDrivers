#include "adx12/frontend_environment.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

static bool
adx12_string_is_present(const char *value)
{
    return value != NULL && value[0] != '\0';
}

static bool
adx12_ascii_equal_range(
    const char *begin,
    const char *end,
    const char *expected)
{
    size_t expected_length = strlen(expected);
    size_t range_length = (size_t)(end - begin);

    while (range_length > 0 && isspace((unsigned char)*begin)) {
        begin++;
        range_length--;
    }
    while (range_length > 0 && isspace((unsigned char)begin[range_length - 1]))
        range_length--;

    if (range_length != expected_length)
        return false;

    for (size_t i = 0; i < range_length; i++) {
        if (tolower((unsigned char)begin[i]) !=
            tolower((unsigned char)expected[i]))
            return false;
    }
    return true;
}

static bool
adx12_override_dll_list_contains(
    const char *begin,
    const char *end,
    const char *dll_name)
{
    const char *item = begin;

    while (item < end) {
        const char *item_end = item;
        while (item_end < end && *item_end != ',')
            item_end++;
        if (adx12_ascii_equal_range(item, item_end, dll_name))
            return true;
        item = item_end < end ? item_end + 1 : end;
    }
    return false;
}

static bool
adx12_frontend_override_has_exact_mode(
    const char *override_list,
    const char *dll_name,
    const char *short_mode,
    const char *long_mode)
{
    const char *entry;
    bool found = false;

    if (!adx12_string_is_present(override_list) ||
        !adx12_string_is_present(dll_name))
        return false;

    entry = override_list;
    while (*entry != '\0') {
        const char *entry_end = strchr(entry, ';');
        const char *equals;
        const char *value_begin;

        if (entry_end == NULL)
            entry_end = entry + strlen(entry);
        equals = memchr(entry, '=', (size_t)(entry_end - entry));
        if (equals != NULL &&
            adx12_override_dll_list_contains(entry, equals, dll_name)) {
            value_begin = equals + 1;
            if (!adx12_ascii_equal_range(
                    value_begin, entry_end, short_mode) &&
                !adx12_ascii_equal_range(
                    value_begin, entry_end, long_mode))
                return false;
            found = true;
        }
        entry = *entry_end == ';' ? entry_end + 1 : entry_end;
    }
    return found;
}

bool
adx12_frontend_override_is_native_only(
    const char *override_list,
    const char *dll_name)
{
    return adx12_frontend_override_has_exact_mode(
        override_list, dll_name, "n", "native");
}

bool
adx12_frontend_override_is_builtin_only(
    const char *override_list,
    const char *dll_name)
{
    return adx12_frontend_override_has_exact_mode(
        override_list, dll_name, "b", "builtin");
}

static bool
adx12_ascii_contains_case_insensitive(const char *value, const char *needle)
{
    size_t value_length;
    size_t needle_length;

    if (value == NULL || needle == NULL)
        return false;
    value_length = strlen(value);
    needle_length = strlen(needle);
    if (needle_length == 0 || needle_length > value_length)
        return false;

    for (size_t i = 0; i + needle_length <= value_length; i++) {
        size_t j = 0;
        while (j < needle_length &&
               tolower((unsigned char)value[i + j]) ==
                   tolower((unsigned char)needle[j]))
            j++;
        if (j == needle_length)
            return true;
    }
    return false;
}

bool
adx12_frontend_renderer_image_is_forbidden(const char *image_name)
{
    static const char *const forbidden[] = {
        "wined3d",
        "winevulkan",
        "dxvk",
        "libvkd3d",
        "vkd3d-proton",
    };

    if (!adx12_string_is_present(image_name))
        return false;

    for (size_t i = 0; i < sizeof(forbidden) / sizeof(forbidden[0]); i++) {
        if (adx12_ascii_contains_case_insensitive(image_name, forbidden[i]))
            return true;
    }
    return false;
}

static void
adx12_copy_renderer_name(char *destination, size_t capacity, const char *source)
{
    const char *basename;
    size_t length;

    if (capacity == 0)
        return;
    basename = strrchr(source, '/');
    basename = basename == NULL ? source : basename + 1;
    length = strlen(basename);
    if (length >= capacity)
        length = capacity - 1;
    memcpy(destination, basename, length);
    destination[length] = '\0';
}

static void
adx12_inspect_loaded_renderers(ADX12FrontendEnvironment *environment)
{
#if defined(__APPLE__)
    uint32_t image_count = _dyld_image_count();
    for (uint32_t i = 0; i < image_count; i++) {
        const char *image_name = _dyld_get_image_name(i);
        if (adx12_frontend_renderer_image_is_forbidden(image_name)) {
            environment->renderer_conflict = true;
            adx12_copy_renderer_name(
                environment->renderer_name,
                sizeof(environment->renderer_name),
                image_name);
            return;
        }
    }
#else
    (void)environment;
#endif
}

ADX12FrontendStatus
adx12_frontend_inspect_environment(ADX12FrontendEnvironment *environment)
{
    const char *overrides;

    if (environment == NULL ||
        environment->struct_size < sizeof(ADX12FrontendEnvironment))
        return ADX12_FRONTEND_INVALID_ARGUMENT;

    memset(environment, 0, sizeof(*environment));
    environment->struct_size = sizeof(*environment);
    environment->abi_version = ADX12_FRONTEND_ENVIRONMENT_ABI;
    environment->has_wine_prefix =
        adx12_string_is_present(getenv("WINEPREFIX"));
    environment->has_crossover_bottle =
        adx12_string_is_present(getenv("CX_BOTTLE"));

    if (environment->has_crossover_bottle ||
        adx12_string_is_present(getenv("CX_ROOT")) ||
        adx12_string_is_present(getenv("CX_INITIALIZED")))
        environment->host = ADX12_HOST_CROSSOVER;
    else if (environment->has_wine_prefix ||
             adx12_string_is_present(getenv("WINESERVER")) ||
             adx12_string_is_present(getenv("WINELOADER")))
        environment->host = ADX12_HOST_WINE;
    else
        environment->host = ADX12_HOST_UNKNOWN;

    overrides = getenv("WINEDLLOVERRIDES");
    environment->native_d3d12_override =
        adx12_frontend_override_is_native_only(overrides, "d3d12");
    environment->native_dxgi_override =
        adx12_frontend_override_is_native_only(overrides, "dxgi");
    environment->builtin_winemetal_override =
        adx12_frontend_override_is_builtin_only(overrides, "winemetal");
    adx12_inspect_loaded_renderers(environment);

    if (environment->renderer_conflict)
        return ADX12_FRONTEND_RENDERER_CONFLICT;
    if (environment->host == ADX12_HOST_UNKNOWN)
        return ADX12_FRONTEND_WINDOWS_HOST_REQUIRED;
    if (!environment->native_d3d12_override ||
        !environment->native_dxgi_override ||
        !environment->builtin_winemetal_override)
        return ADX12_FRONTEND_PROJECT_DLL_OVERRIDE_REQUIRED;
    return ADX12_FRONTEND_OK;
}

const char *
adx12_frontend_status_string(ADX12FrontendStatus status)
{
    switch (status) {
    case ADX12_FRONTEND_OK:
        return "ready";
    case ADX12_FRONTEND_INVALID_ARGUMENT:
        return "invalid argument";
    case ADX12_FRONTEND_WINDOWS_HOST_REQUIRED:
        return "Wine or CrossOver host required";
    case ADX12_FRONTEND_PROJECT_DLL_OVERRIDE_REQUIRED:
        return "native ADX12 D3D12/DXGI and builtin WineMetal required";
    case ADX12_FRONTEND_RENDERER_CONFLICT:
        return "competing renderer already loaded";
    }
    return "unknown status";
}

const char *
adx12_frontend_host_string(ADX12HostEnvironment host)
{
    switch (host) {
    case ADX12_HOST_WINE:
        return "wine";
    case ADX12_HOST_CROSSOVER:
        return "crossover";
    case ADX12_HOST_UNKNOWN:
        return "unknown";
    }
    return "unknown";
}
