/* Experimental US v304 Aroma companion. No Nintendo code is bundled. */
#include <wups.h>
/* Settings and storage usage follows the official example plugin:
 * https://github.com/wiiu-env/WiiUPluginSystem/blob/main/plugins/example_plugin/src/main.c */
#include <wups/config/WUPSConfigItemBoolean.h>
#undef DECL_FUNCTION
#include <function_patcher/function_patching.h>
#include <coreinit/debug.h>
#include <coreinit/dynload.h>
#include <coreinit/memorymap.h>
#include <coreinit/title.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "guard.h"

WUPS_PLUGIN_NAME("Hidden CSS Giga Respawn (experimental)");
WUPS_PLUGIN_DESCRIPTION("US v304 only, experimental: restore hidden forms 58-61 after entry init (KO only seen for Giga Bowser)");
WUPS_PLUGIN_VERSION("0.1.0-experimental");
WUPS_PLUGIN_AUTHOR("CSS Expansion Contributors");
WUPS_PLUGIN_LICENSE("MIT");

WUPS_USE_STORAGE("hidden_css_respawn");

#define SETTING_ENABLED_KEY "enabled"
#define SETTING_LABEL "Enable respawn hook (experimental, US v304)"

#define ENTRY_INIT_ADDRESS 0x02E45558u
#define GIGA_ASSIGNMENT_ADDRESS 0x02E45708u

static uint32_t (*real_initialize_entry)(void *);
static guard_state_t guard_state;
static const uint64_t allowed_titles[] = {GUARD_TITLE_US_V304};
static const char *target_name;
static uint32_t target_offset;
static uint32_t entry_range_lo;
static uint32_t entry_range_size;
static FunctionPatcherStatus last_remove_status;
/* Resolved plugin setting. Starts OFF, so nothing is enabled before the setting is read. */
static bool setting_wanted = false;
static bool rejection_logged = false;

static uint32_t my_initialize_entry(void *entry) {
    /* Preserve the native r3 return even if callers treat this routine as void. */
    uint32_t result = real_initialize_entry(entry);
    if (!guard_may_read_entry(guard_state.enabled, (uint32_t)(uintptr_t)entry, entry_range_lo, entry_range_size)) {
        if (guard_state.enabled && guard_first_rejection(&rejection_logged)) {
            OSReport("[HiddenCSS] First entry rejected by the range check (entry 0x%08X, range 0x%08X+0x%X): hook left idle for it.\n",
                     (unsigned)(uintptr_t)entry, (unsigned)entry_range_lo, (unsigned)entry_range_size);
        }
        return result;
    }
    volatile uint32_t *kind = (volatile uint32_t *)((uint8_t *)entry + 0x10);
    volatile uint32_t *alternate = (volatile uint32_t *)((uint8_t *)entry + 0x14);
    uint32_t kind_value = *kind;
    uint32_t current = *alternate;
    uint32_t restored = guard_restored_alternate(kind_value, current);
    if (restored != current) {
        *alternate = restored;
    }
    return result;
}

static bool code_matches(void) {
    const volatile uint32_t *start = (const volatile uint32_t *)ENTRY_INIT_ADDRESS;
    const volatile uint32_t *assignment = (const volatile uint32_t *)GIGA_ASSIGNMENT_ADDRESS;
    uint32_t words[GUARD_SIGNATURE_WORDS];
    for (uint32_t i = 0; i < 4u; ++i) {
        words[i] = start[i];
        words[4u + i] = assignment[i];
    }
    return guard_signature_ok(words);
}

static bool find_executable(uint32_t *text_addr, uint32_t *text_size, const char **name) {
    int count = OSDynLoad_GetNumberOfRPLs();
    if (count <= 0 || count > 256) return false;
    for (int i = 0; i < count; ++i) {
        OSDynLoad_NotifyData module;
        if (!OSDynLoad_GetRPLInfo((uint32_t)i, 1, &module) || module.name == NULL) continue;
        const char *base = strrchr(module.name, '/');
        base = base ? base + 1 : module.name;
        if (strcmp(base, "cross_f.rpx") != 0 && strcmp(base, "cross_f") != 0) continue;
        *text_addr = module.textAddr;
        *text_size = module.textSize;
        *name = strcmp(base, "cross_f.rpx") == 0 ? "cross_f.rpx" : "cross_f";
        return true;
    }
    return false;
}

/* Gathers preflight facts. Memory is read only after the title matches and the bounds pass. */
static void collect_facts(guard_facts_t *facts) {
    uint32_t text_addr = 0u;
    uint32_t text_size = 0u;
    const char *name = NULL;
    facts->title_id = OSGetTitleID();
    if (!guard_title_allowed(facts->title_id)) return;
    facts->executable_found = find_executable(&text_addr, &text_size, &name);
    facts->bounds_ok = facts->executable_found &&
        guard_bounds_ok(text_addr, text_size, ENTRY_INIT_ADDRESS, GIGA_ASSIGNMENT_ADDRESS);
    facts->signature_ok = facts->bounds_ok && code_matches();
    OSGetMapVirtAddrRange(&facts->range_lo, &facts->range_size);
    if (facts->bounds_ok) {
        target_name = name;
        target_offset = ENTRY_INIT_ADDRESS - text_addr;
    }
    entry_range_lo = facts->range_lo;
    entry_range_size = facts->range_size;
}

static bool op_init_library(void) {
    return FunctionPatcher_InitLibrary() == FUNCTION_PATCHER_RESULT_SUCCESS;
}

static void op_deinit_library(void) {
    FunctionPatcher_DeInitLibrary();
}

static bool op_add_patch(uint32_t *handle, bool *applied) {
    function_replacement_data_t replacement =
        REPLACE_FUNCTION_OF_EXECUTABLE_BY_ADDRESS_WITH_VERSION(initialize_entry,
            allowed_titles, 1, target_name, target_offset, 304, 304);
    return FunctionPatcher_AddFunctionPatch(&replacement, handle, applied) == FUNCTION_PATCHER_RESULT_SUCCESS;
}

static bool op_remove_patch(uint32_t handle) {
    last_remove_status = FunctionPatcher_RemoveFunctionPatch(handle);
    return last_remove_status == FUNCTION_PATCHER_RESULT_SUCCESS;
}

static void op_log(guard_reason_t reason) {
    if (reason == GUARD_REMOVE_FAILED) {
        OSReport("[HiddenCSS] Hook removal failed: %s\n", FunctionPatcher_GetStatusStr(last_remove_status));
        return;
    }
    OSReport("%s", guard_reason_text(reason));
}

static const guard_ops_t guard_ops = {op_init_library, op_add_patch, op_remove_patch, op_deinit_library, op_log};

static void apply_setting(void) {
    guard_facts_t facts = {0};
    collect_facts(&facts);
    guard_apply(&guard_state, &guard_ops, &facts, setting_wanted);
}

static void log_storage_error(const char *what, WUPSStorageError status) {
    OSReport("[HiddenCSS] %s failed: %s\n", what, WUPSStorageAPI_GetStatusStr(status));
}

static void store_setting(bool value) {
    WUPSStorageError status = WUPSStorageAPI_StoreBool(NULL, SETTING_ENABLED_KEY, value);
    if (status != WUPS_STORAGE_ERROR_SUCCESS) log_storage_error("Setting store", status);
}

static void save_settings(void) {
    WUPSStorageError status = WUPSStorageAPI_SaveStorage(false);
    if (status != WUPS_STORAGE_ERROR_SUCCESS) log_storage_error("Setting save", status);
}

/* A missing key stores the default. Any other read error leaves the setting OFF (fail closed). */
static void load_setting(void) {
    bool stored = false;
    WUPSStorageError status = WUPSStorageAPI_GetBool(NULL, SETTING_ENABLED_KEY, &stored);
    bool read_ok = status == WUPS_STORAGE_ERROR_SUCCESS || status == WUPS_STORAGE_ERROR_NOT_FOUND;
    bool found = status == WUPS_STORAGE_ERROR_SUCCESS;
    setting_wanted = guard_config_resolve(read_ok, found, stored);
    if (status == WUPS_STORAGE_ERROR_NOT_FOUND) {
        store_setting(GUARD_DEFAULT_ENABLED);
        save_settings();
    } else if (!read_ok) {
        log_storage_error("Setting read", status);
    }
}

static void on_setting_changed(ConfigItemBoolean *item, bool new_value) {
    (void)item;
    setting_wanted = new_value;
    store_setting(new_value);
    apply_setting();
}

static WUPSConfigAPICallbackStatus menu_build_failed(const char *step) {
    OSReport("[HiddenCSS] Config menu build failed at %s: respawn setting not shown.\n", step);
    return WUPSCONFIG_API_CALLBACK_RESULT_ERROR;
}

static WUPSConfigAPICallbackStatus on_menu_opened(WUPSConfigCategoryHandle root) {
    /* The item sits directly on the plugin's root page, so no sub-category has to be opened to see it.
     * It shows ON while a failed removal still holds the patch, so the menu never hides a live hook. */
    bool shown = setting_wanted || guard_state.has_handle;
    if (WUPSConfigItemBoolean_AddToCategory(root, SETTING_ENABLED_KEY, SETTING_LABEL, GUARD_DEFAULT_ENABLED,
                                            shown, on_setting_changed) != WUPSCONFIG_API_RESULT_SUCCESS) {
        return menu_build_failed("item add");
    }
    return WUPSCONFIG_API_CALLBACK_RESULT_SUCCESS;
}

static void on_menu_closed(void) {
    save_settings();
    apply_setting();
}

/* Gets called once when the plugin is loaded. If the config API fails, the setting stays OFF. */
INITIALIZE_PLUGIN() {
    WUPSConfigAPIOptionsV1 options = {.name = "Hidden CSS respawn"};
    if (WUPSConfigAPI_Init(options, on_menu_opened, on_menu_closed) != WUPSCONFIG_API_RESULT_SUCCESS) {
        OSReport("[HiddenCSS] Config API init failed: respawn hook stays off.\n");
        return;
    }
    load_setting();
}

ON_APPLICATION_START() {
    guard_disable(&guard_state, &guard_ops);
    apply_setting();
}

ON_APPLICATION_ENDS() { guard_disable(&guard_state, &guard_ops); }
DEINITIALIZE_PLUGIN() { guard_disable(&guard_state, &guard_ops); }
