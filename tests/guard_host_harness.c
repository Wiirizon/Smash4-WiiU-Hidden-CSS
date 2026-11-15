/* Host harness for companion/guard.h: pure rules and the enable/disable state machine.
 * tests/test_guard_host.py compiles and runs it. Prints one line per failed check to stderr
 * and exits nonzero if any check fails. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "form_restore.h"
#include "guard.h"

#define RANGE_LO UINT32_C(0x10000000)
#define RANGE_SIZE UINT32_C(0x00100000)
#define TEXT_BASE UINT32_C(0x02E00000)
#define TEXT_SIZE UINT32_C(0x00100000)
#define FAKE_HANDLE UINT32_C(7)
#define LOG_CAP 16u
#define TRACE_CAP 64u

static int checks;
static int failures;

#define CHECK(cond)                                                    \
    do {                                                               \
        checks++;                                                      \
        if (!(cond)) {                                                 \
            failures++;                                                \
            fprintf(stderr, "FAILED line %d: %s\n", __LINE__, #cond);  \
        }                                                              \
    } while (0)

/* ---- Title gate ---- */
static void test_title_gate(void) {
    CHECK(guard_title_allowed(UINT64_C(0x0005000010144F00)));
    CHECK(!guard_title_allowed(UINT64_C(0)));
    CHECK(!guard_title_allowed(UINT64_C(0x0005000010144F01)));
    CHECK(!guard_title_allowed(UINT64_C(0x0005000010144EFF)));
    CHECK(!guard_title_allowed(UINT64_C(0x8005000010144F00)));
    CHECK(!guard_title_allowed(UINT64_MAX));
}

/* ---- Text bounds ---- */
static void test_bounds(void) {
    uint32_t last = TEXT_BASE + TEXT_SIZE - 16u;
    uint32_t past = TEXT_BASE + TEXT_SIZE - 15u;
    CHECK(guard_bounds_ok(TEXT_BASE, TEXT_SIZE, TEXT_BASE, TEXT_BASE));
    CHECK(guard_bounds_ok(TEXT_BASE, TEXT_SIZE, last, last));
    CHECK(!guard_bounds_ok(TEXT_BASE, TEXT_SIZE, past, last));
    CHECK(!guard_bounds_ok(TEXT_BASE, TEXT_SIZE, last, past));
    CHECK(!guard_bounds_ok(TEXT_BASE, TEXT_SIZE, TEXT_BASE - 4u, TEXT_BASE));
    CHECK(!guard_bounds_ok(TEXT_BASE, 15u, TEXT_BASE, TEXT_BASE));
    CHECK(!guard_bounds_ok(TEXT_BASE, 0u, TEXT_BASE, TEXT_BASE));
    CHECK(!guard_bounds_ok(UINT32_C(0xFFFFFFF0), UINT32_C(0x20), UINT32_C(0xFFFFFFF0), UINT32_C(0xFFFFFFF0)));
    CHECK(!guard_bounds_ok(UINT32_C(0x02000000), UINT32_C(0x00100000), UINT32_C(0x02E45558), UINT32_C(0x02E45708)));
    CHECK(guard_bounds_ok(UINT32_C(0x02E40000), UINT32_C(0x00010000), UINT32_C(0x02E45558), UINT32_C(0x02E45708)));
}

/* ---- Signature words ---- */
static const uint32_t good_words[GUARD_SIGNATURE_WORDS] = {
    0x7C0802A6u, 0x9421FFF0u, 0x93E1000Cu, 0x7C7F1B78u,
    0x398000CCu, 0x3800000Fu, 0x999F14EEu, 0x901F0014u,
};

static void test_signature(void) {
    uint32_t words[GUARD_SIGNATURE_WORDS];
    uint32_t swapped[GUARD_SIGNATURE_WORDS];
    uint32_t zero[GUARD_SIGNATURE_WORDS] = {0};
    memcpy(words, good_words, sizeof words);
    CHECK(guard_signature_ok(words));
    for (uint32_t i = 0; i < GUARD_SIGNATURE_WORDS; ++i) {
        memcpy(words, good_words, sizeof words);
        words[i] ^= 1u;
        CHECK(!guard_signature_ok(words));
    }
    memcpy(swapped, good_words + 4, 4 * sizeof(uint32_t));
    memcpy(swapped + 4, good_words, 4 * sizeof(uint32_t));
    CHECK(!guard_signature_ok(swapped));
    CHECK(!guard_signature_ok(zero));
}

/* ---- Entry pointer ---- */
static void test_entry_ptr(void) {
    uint32_t end = RANGE_LO + RANGE_SIZE;
    CHECK(guard_entry_ptr_ok(RANGE_LO, RANGE_LO, RANGE_SIZE));
    CHECK(guard_entry_ptr_ok(end - 0x18u, RANGE_LO, RANGE_SIZE));
    CHECK(!guard_entry_ptr_ok(0u, RANGE_LO, RANGE_SIZE));
    CHECK(!guard_entry_ptr_ok(RANGE_LO + 1u, RANGE_LO, RANGE_SIZE));
    CHECK(!guard_entry_ptr_ok(RANGE_LO + 2u, RANGE_LO, RANGE_SIZE));
    CHECK(!guard_entry_ptr_ok(RANGE_LO + 3u, RANGE_LO, RANGE_SIZE));
    CHECK(!guard_entry_ptr_ok(RANGE_LO - 4u, RANGE_LO, RANGE_SIZE));
    CHECK(!guard_entry_ptr_ok(end, RANGE_LO, RANGE_SIZE));
    CHECK(!guard_entry_ptr_ok(end - 0x14u, RANGE_LO, RANGE_SIZE));
    CHECK(!guard_entry_ptr_ok(end - 0x17u, RANGE_LO, RANGE_SIZE));
    CHECK(!guard_entry_ptr_ok(UINT32_C(0xFFFFFFFC), RANGE_LO, RANGE_SIZE));
    CHECK(!guard_entry_ptr_ok(UINT32_C(0xFFFFFFE8), RANGE_LO, RANGE_SIZE));
    CHECK(!guard_entry_ptr_ok(RANGE_LO, RANGE_LO, 0u));
    CHECK(!guard_entry_ptr_ok(UINT32_C(0xFFFFFF00), UINT32_C(0xFFF00000), UINT32_C(0x00200000)));
}

/* ---- Enabled-forms gate ---- */
static void test_enabled_forms(void) {
    uint32_t enabled_count = 0u;
    CHECK(guard_form_enabled(58u));
    CHECK(!guard_form_enabled(59u));
    CHECK(!guard_form_enabled(60u));
    CHECK(!guard_form_enabled(61u));
    CHECK(!guard_form_enabled(64u));
    CHECK(!guard_form_enabled(UINT32_C(0xFFFFFFFF)));
    for (uint32_t kind = 0; kind <= 255u; ++kind) {
        if (guard_form_enabled(kind)) {
            enabled_count++;
            CHECK(kind == 58u);
        }
    }
    CHECK(enabled_count == 1u);
}

static void test_may_read_entry(void) {
    const uint32_t lo = UINT32_C(0x10000000);
    const uint32_t size = UINT32_C(0x01000000);
    const uint32_t good = lo + UINT32_C(0x100);
    CHECK(guard_may_read_entry(true, good, lo, size));
    CHECK(!guard_may_read_entry(false, good, lo, size));
    CHECK(!guard_may_read_entry(true, 0u, lo, size));
    CHECK(!guard_may_read_entry(true, good + 1u, lo, size));
    CHECK(!guard_may_read_entry(true, lo - 4u, lo, size));
    CHECK(!guard_may_read_entry(true, lo + size, lo, size));
    CHECK(!guard_may_read_entry(true, good, 0u, 0u));
    CHECK(!guard_may_read_entry(false, 0u, 0u, 0u));
}

static void test_restored_alternate_never_writes_outside_giga(void) {
    CHECK(guard_restored_alternate(58u, 15u) == 58u);
    CHECK(guard_restored_alternate(58u, 58u) == 58u);
    CHECK(guard_restored_alternate(58u, 16u) == 16u);
    CHECK(guard_restored_alternate(59u, 22u) == 22u);
    CHECK(guard_restored_alternate(60u, 41u) == 41u);
    CHECK(guard_restored_alternate(61u, 30u) == 30u);
    CHECK(guard_restored_alternate(UINT32_C(0xFFFFFFFF), 15u) == 15u);
}

static void test_combined_restore_changes_only_giga(void) {
    uint32_t changed = 0u;
    bool only_giga = true;
    for (uint32_t kind = 0; kind <= 255u; ++kind) {
        for (uint32_t alternate = 0; alternate <= 255u; ++alternate) {
            uint32_t got = guard_restored_alternate(kind, alternate);
            if (got != alternate) {
                changed++;
                if (!(kind == 58u && alternate == 15u && got == 58u)) only_giga = false;
            }
        }
    }
    CHECK(changed == 1u);
    CHECK(only_giga);
}

/* ---- Form table: all four pairs, independent of the enabled mask ---- */
static void test_form_table_maps_all_four_forms(void) {
    CHECK(sizeof form_restore_pairs / sizeof form_restore_pairs[0] == 4u);
    CHECK(form_restore_alternate(58u, 15u) == 58u);
    CHECK(form_restore_alternate(59u, 22u) == 59u);
    CHECK(form_restore_alternate(60u, 41u) == 60u);
    CHECK(form_restore_alternate(61u, 30u) == 61u);
    /* A form paired with another form's base is not restored. */
    CHECK(form_restore_alternate(59u, 15u) == 15u);
    CHECK(form_restore_alternate(60u, 22u) == 22u);
    CHECK(form_restore_alternate(61u, 41u) == 41u);
    CHECK(form_restore_alternate(58u, 30u) == 30u);
    /* IDs outside the table are never restored. */
    CHECK(form_restore_alternate(62u, 30u) == 30u);
    CHECK(form_restore_alternate(57u, 15u) == 15u);
    /* Disabled forms 59-61 never restore through the guard, even with their correct base. */
    CHECK(guard_restored_alternate(59u, 22u) == 22u);
    CHECK(guard_restored_alternate(60u, 41u) == 41u);
    CHECK(guard_restored_alternate(61u, 30u) == 30u);
}

/* ---- Reason texts ---- */
static void test_first_rejection(void) {
    bool seen = false;
    CHECK(guard_first_rejection(&seen));
    CHECK(!guard_first_rejection(&seen));
    CHECK(!guard_first_rejection(&seen));
}

static void test_reason_texts(void) {
    for (int r = GUARD_OK; r <= GUARD_CONFIG_OFF; ++r) {
        const char *text = guard_reason_text((guard_reason_t)r);
        CHECK(text != NULL);
        CHECK(text != NULL && strlen(text) > 0u && text[strlen(text) - 1u] == '\n');
    }
    CHECK(strcmp(guard_reason_text(GUARD_OK),
                 "[HiddenCSS] Experimental hidden-form respawn hook armed for US v304; entry range is checked on each call.\n") == 0);
    CHECK(strcmp(guard_reason_text(GUARD_CONFIG_OFF),
                 "[HiddenCSS] Respawn hook inactive: disabled in plugin settings.\n") == 0);
}

/* ---- Fake FunctionPatcher ops and state machine ---- */
static char trace[TRACE_CAP];
static unsigned trace_len;
static guard_reason_t logged[LOG_CAP];
static unsigned log_count;
static guard_state_t *fake_state;
static bool fake_init_ok;
static bool fake_add_ok;
static bool fake_applied;
static uint32_t fake_handle;
static bool fake_remove_ok;
static unsigned remove_count;
static uint32_t last_removed;
static bool enabled_at_add;
static bool enabled_at_remove;

static void push(char op) {
    if (trace_len + 1u < TRACE_CAP) {
        trace[trace_len++] = op;
        trace[trace_len] = '\0';
    }
}

static bool fake_init(void) {
    push('I');
    return fake_init_ok;
}

static bool fake_add(uint32_t *handle, bool *applied) {
    push('A');
    enabled_at_add = fake_state->enabled;
    *handle = fake_handle;
    *applied = fake_applied;
    return fake_add_ok;
}

static bool fake_remove(uint32_t handle) {
    push('R');
    enabled_at_remove = fake_state->enabled;
    last_removed = handle;
    remove_count++;
    return fake_remove_ok;
}

static void fake_deinit(void) {
    push('D');
}

static void fake_log(guard_reason_t reason) {
    if (log_count < LOG_CAP) logged[log_count] = reason;
    log_count++;
}

static const guard_ops_t fake_ops = {fake_init, fake_add, fake_remove, fake_deinit, fake_log};

static void fake_reset(guard_state_t *st) {
    fake_state = st;
    st->enabled = false;
    st->lib_ready = false;
    st->has_handle = false;
    st->handle = 0u;
    trace_len = 0u;
    trace[0] = '\0';
    log_count = 0u;
    fake_init_ok = true;
    fake_add_ok = true;
    fake_applied = true;
    fake_handle = FAKE_HANDLE;
    fake_remove_ok = true;
    remove_count = 0u;
    last_removed = 0u;
    enabled_at_add = false;
    enabled_at_remove = false;
}

static guard_facts_t good_facts(void) {
    guard_facts_t facts = {
        .title_id = GUARD_TITLE_US_V304,
        .executable_found = true,
        .bounds_ok = true,
        .signature_ok = true,
        .range_lo = RANGE_LO,
        .range_size = RANGE_SIZE,
    };
    return facts;
}

static bool traced(const char *want) {
    return strcmp(trace, want) == 0;
}

static bool logged_exactly(guard_reason_t reason) {
    return log_count == 1u && logged[0] == reason;
}

static void expect_refused(guard_state_t *st, const guard_facts_t *facts, guard_reason_t reason) {
    fake_reset(st);
    CHECK(guard_enable(st, &fake_ops, facts) == reason);
    CHECK(traced(""));
    CHECK(!st->enabled);
    CHECK(logged_exactly(reason));
}

static void test_preflight_each_reason(void) {
    guard_state_t st = {0};
    guard_facts_t facts = good_facts();
    facts.title_id = GUARD_TITLE_US_V304 + 1u;
    expect_refused(&st, &facts, GUARD_TITLE_MISMATCH);
    facts = good_facts();
    facts.executable_found = false;
    expect_refused(&st, &facts, GUARD_EXECUTABLE_NOT_FOUND);
    facts = good_facts();
    facts.bounds_ok = false;
    expect_refused(&st, &facts, GUARD_BOUNDS_FAILED);
    facts = good_facts();
    facts.signature_ok = false;
    expect_refused(&st, &facts, GUARD_SIGNATURE_MISMATCH);
    facts = good_facts();
    facts.range_size = 0u;
    expect_refused(&st, &facts, GUARD_RANGE_UNKNOWN);
    facts = good_facts();
    facts.range_lo = UINT32_C(0xFFF00000);
    facts.range_size = UINT32_C(0x00200000);
    expect_refused(&st, &facts, GUARD_RANGE_UNKNOWN);
}

static void test_enable_success(void) {
    guard_state_t st = {0};
    guard_facts_t facts = good_facts();
    fake_reset(&st);
    CHECK(guard_enable(&st, &fake_ops, &facts) == GUARD_OK);
    CHECK(traced("IA"));
    CHECK(!enabled_at_add);
    CHECK(st.enabled);
    CHECK(st.lib_ready && st.has_handle && st.handle == FAKE_HANDLE);
    CHECK(logged_exactly(GUARD_OK));
    CHECK(remove_count == 0u);
}

static void test_lib_init_failure(void) {
    guard_state_t st = {0};
    guard_facts_t facts = good_facts();
    fake_reset(&st);
    fake_init_ok = false;
    CHECK(guard_enable(&st, &fake_ops, &facts) == GUARD_LIB_INIT_FAILED);
    CHECK(traced("I"));
    CHECK(!st.enabled && !st.lib_ready);
    CHECK(logged_exactly(GUARD_LIB_INIT_FAILED));
}

static void test_add_status_failure(void) {
    guard_state_t st = {0};
    guard_facts_t facts = good_facts();
    fake_reset(&st);
    fake_add_ok = false;
    fake_handle = 0u;
    CHECK(guard_enable(&st, &fake_ops, &facts) == GUARD_PATCH_ADD_FAILED);
    CHECK(traced("IAD"));
    CHECK(remove_count == 0u);
    CHECK(!st.enabled && !st.has_handle && !st.lib_ready);
    CHECK(logged_exactly(GUARD_PATCH_ADD_FAILED));
}

static void test_add_not_applied_with_handle(void) {
    guard_state_t st = {0};
    guard_facts_t facts = good_facts();
    fake_reset(&st);
    fake_applied = false;
    CHECK(guard_enable(&st, &fake_ops, &facts) == GUARD_PATCH_NOT_APPLIED);
    CHECK(traced("IARD"));
    CHECK(last_removed == FAKE_HANDLE);
    CHECK(!st.enabled && !st.has_handle && !st.lib_ready);
    CHECK(logged_exactly(GUARD_PATCH_NOT_APPLIED));
}

static void test_add_null_handle(void) {
    guard_state_t st = {0};
    guard_facts_t facts = good_facts();
    fake_reset(&st);
    fake_handle = 0u;
    CHECK(guard_enable(&st, &fake_ops, &facts) == GUARD_PATCH_HANDLE_NULL);
    CHECK(traced("IAD"));
    CHECK(remove_count == 0u);
    CHECK(!st.enabled);
    CHECK(logged_exactly(GUARD_PATCH_HANDLE_NULL));
}

static void test_remove_failure_then_stuck(void) {
    guard_state_t st = {0};
    guard_facts_t facts = good_facts();
    fake_reset(&st);
    fake_applied = false;
    fake_remove_ok = false;
    CHECK(guard_enable(&st, &fake_ops, &facts) == GUARD_PATCH_NOT_APPLIED);
    CHECK(traced("IAR"));
    CHECK(st.has_handle && st.lib_ready && !st.enabled);
    CHECK(log_count == 2u && logged[0] == GUARD_PATCH_NOT_APPLIED && logged[1] == GUARD_REMOVE_FAILED);

    fake_applied = true;
    CHECK(guard_enable(&st, &fake_ops, &facts) == GUARD_STUCK);
    CHECK(traced("IARR"));
    CHECK(log_count == 3u && logged[2] == GUARD_STUCK);

    fake_remove_ok = true;
    CHECK(guard_enable(&st, &fake_ops, &facts) == GUARD_OK);
    CHECK(traced("IARRRDIA"));
    CHECK(st.enabled && st.has_handle && st.lib_ready);
    CHECK(log_count == 4u && logged[3] == GUARD_OK);

    guard_disable(&st, &fake_ops);
    CHECK(traced("IARRRDIARD"));
    CHECK(!st.has_handle && !st.lib_ready && !st.enabled);
    CHECK(remove_count == 4u);
}

static void test_disable_after_success(void) {
    guard_state_t st = {0};
    guard_facts_t facts = good_facts();
    fake_reset(&st);
    CHECK(guard_enable(&st, &fake_ops, &facts) == GUARD_OK);
    guard_disable(&st, &fake_ops);
    CHECK(traced("IARD"));
    CHECK(!enabled_at_remove);
    CHECK(last_removed == FAKE_HANDLE);
    CHECK(!st.enabled && !st.has_handle && !st.lib_ready);
    guard_disable(&st, &fake_ops);
    CHECK(traced("IARD"));
    CHECK(remove_count == 1u);
}

static void test_double_enable_is_noop(void) {
    guard_state_t st = {0};
    guard_facts_t facts = good_facts();
    fake_reset(&st);
    CHECK(guard_enable(&st, &fake_ops, &facts) == GUARD_OK);
    CHECK(guard_enable(&st, &fake_ops, &facts) == GUARD_OK);
    CHECK(traced("IA"));
    CHECK(log_count == 1u);
}

static void test_reenable_after_disable_reinits(void) {
    guard_state_t st = {0};
    guard_facts_t facts = good_facts();
    fake_reset(&st);
    CHECK(guard_enable(&st, &fake_ops, &facts) == GUARD_OK);
    guard_disable(&st, &fake_ops);
    CHECK(guard_enable(&st, &fake_ops, &facts) == GUARD_OK);
    CHECK(traced("IARDIA"));
    CHECK(st.enabled);
}

static void test_disable_removes_held_handle_without_enable(void) {
    guard_state_t st = {0};
    fake_reset(&st);
    st.lib_ready = true;
    st.has_handle = true;
    st.handle = 9u;
    guard_disable(&st, &fake_ops);
    CHECK(traced("RD"));
    CHECK(last_removed == 9u);
    CHECK(!st.has_handle && !st.lib_ready);
}

static void test_remove_once_per_added_patch(void) {
    guard_state_t st = {0};
    guard_facts_t facts = good_facts();
    fake_reset(&st);
    CHECK(guard_enable(&st, &fake_ops, &facts) == GUARD_OK);
    guard_disable(&st, &fake_ops);
    CHECK(guard_enable(&st, &fake_ops, &facts) == GUARD_OK);
    guard_disable(&st, &fake_ops);
    CHECK(remove_count == 2u);
    CHECK(traced("IARDIARD"));
}

static void test_disable_removal_failure_keeps_library(void) {
    guard_state_t st = {0};
    guard_facts_t facts = good_facts();
    fake_reset(&st);
    CHECK(guard_enable(&st, &fake_ops, &facts) == GUARD_OK);
    log_count = 0u;
    fake_remove_ok = false;
    guard_disable(&st, &fake_ops);
    CHECK(traced("IAR"));
    CHECK(!st.enabled && st.has_handle && st.lib_ready);
    CHECK(logged_exactly(GUARD_REMOVE_FAILED));
}

/* ---- Plugin setting: resolved on/off value ---- */
static void test_config_resolve_truth_table(void) {
    CHECK(!guard_config_resolve(false, false, false));
    CHECK(!guard_config_resolve(false, false, true));
    CHECK(!guard_config_resolve(false, true, false));
    CHECK(!guard_config_resolve(false, true, true));
    CHECK(guard_config_resolve(true, false, false) == GUARD_DEFAULT_ENABLED);
    CHECK(guard_config_resolve(true, false, true) == GUARD_DEFAULT_ENABLED);
    CHECK(!guard_config_resolve(true, true, false));
    CHECK(guard_config_resolve(true, true, true));
}

static void test_default_setting_is_off(void) {
    CHECK(!GUARD_DEFAULT_ENABLED);
}

/* ---- Plugin setting: apply(wanted) state machine ---- */
static void test_apply_off_fresh_state_never_adds(void) {
    guard_state_t st = {0};
    guard_facts_t facts = good_facts();
    fake_reset(&st);
    CHECK(guard_apply(&st, &fake_ops, &facts, false) == GUARD_CONFIG_OFF);
    CHECK(traced(""));
    CHECK(!st.enabled && !st.has_handle && !st.lib_ready);
    CHECK(logged_exactly(GUARD_CONFIG_OFF));
}

static void test_apply_on_then_off_removes_before_flag_clears(void) {
    guard_state_t st = {0};
    guard_facts_t facts = good_facts();
    fake_reset(&st);
    CHECK(guard_apply(&st, &fake_ops, &facts, true) == GUARD_OK);
    CHECK(guard_apply(&st, &fake_ops, &facts, false) == GUARD_CONFIG_OFF);
    CHECK(traced("IARD"));
    CHECK(!enabled_at_remove);
    CHECK(last_removed == FAKE_HANDLE);
    CHECK(!st.enabled && !st.has_handle && !st.lib_ready);
    CHECK(log_count == 2u && logged[0] == GUARD_OK && logged[1] == GUARD_CONFIG_OFF);
}

static void test_apply_off_removal_failure_keeps_handle(void) {
    guard_state_t st = {0};
    guard_facts_t facts = good_facts();
    fake_reset(&st);
    CHECK(guard_apply(&st, &fake_ops, &facts, true) == GUARD_OK);
    log_count = 0u;
    fake_remove_ok = false;
    CHECK(guard_apply(&st, &fake_ops, &facts, false) == GUARD_REMOVE_FAILED);
    CHECK(traced("IAR"));
    CHECK(!st.enabled && st.has_handle && st.lib_ready);
    CHECK(logged_exactly(GUARD_REMOVE_FAILED));
    CHECK(guard_apply(&st, &fake_ops, &facts, true) == GUARD_STUCK);
    CHECK(traced("IARR"));
    CHECK(!st.enabled);
    fake_remove_ok = true;
    CHECK(guard_apply(&st, &fake_ops, &facts, false) == GUARD_CONFIG_OFF);
    CHECK(traced("IARRRD"));
    CHECK(!st.has_handle && !st.lib_ready && !st.enabled);
}

static void test_apply_toggle_cycles_leave_nothing_held(void) {
    guard_state_t st = {0};
    guard_facts_t facts = good_facts();
    fake_reset(&st);
    for (unsigned cycle = 0; cycle < 3u; ++cycle) {
        CHECK(guard_apply(&st, &fake_ops, &facts, true) == GUARD_OK);
        CHECK(guard_apply(&st, &fake_ops, &facts, false) == GUARD_CONFIG_OFF);
        CHECK(!st.enabled && !st.has_handle && !st.lib_ready);
    }
    CHECK(traced("IARDIARDIARD"));
    CHECK(remove_count == 3u);
}

static void test_apply_on_failed_preflight_holds_nothing(void) {
    guard_state_t st = {0};
    guard_facts_t facts = good_facts();
    fake_reset(&st);
    facts.signature_ok = false;
    CHECK(guard_apply(&st, &fake_ops, &facts, true) == GUARD_SIGNATURE_MISMATCH);
    CHECK(traced(""));
    CHECK(!st.enabled && !st.has_handle && !st.lib_ready);
    CHECK(logged_exactly(GUARD_SIGNATURE_MISMATCH));
}

int main(void) {
    test_title_gate();
    test_bounds();
    test_signature();
    test_entry_ptr();
    test_enabled_forms();
    test_may_read_entry();
    test_restored_alternate_never_writes_outside_giga();
    test_combined_restore_changes_only_giga();
    test_form_table_maps_all_four_forms();
    test_first_rejection();
    test_reason_texts();
    test_preflight_each_reason();
    test_enable_success();
    test_lib_init_failure();
    test_add_status_failure();
    test_add_not_applied_with_handle();
    test_add_null_handle();
    test_remove_failure_then_stuck();
    test_disable_after_success();
    test_double_enable_is_noop();
    test_reenable_after_disable_reinits();
    test_disable_removes_held_handle_without_enable();
    test_remove_once_per_added_patch();
    test_disable_removal_failure_keeps_library();
    test_config_resolve_truth_table();
    test_default_setting_is_off();
    test_apply_off_fresh_state_never_adds();
    test_apply_on_then_off_removes_before_flag_clears();
    test_apply_off_removal_failure_keeps_handle();
    test_apply_toggle_cycles_leave_nothing_held();
    test_apply_on_failed_preflight_holds_nothing();
    if (failures != 0) {
        fprintf(stderr, "%d of %d checks failed\n", failures, checks);
        return 1;
    }
    printf("guard host harness: %d checks passed\n", checks);
    return 0;
}
