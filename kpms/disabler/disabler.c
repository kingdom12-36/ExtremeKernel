/* SPDX-License-Identifier: GPL-2.0 */
/*
 * disabler.kpm  v1.1  -  ExtremeKernel Runtime Debug Disabler
 * Samsung Galaxy S20 Ultra (d2s, Exynos 9825), kernel 4.14
 *
 * Disables kernel debug/panic features at RUNTIME — no defconfig edits needed.
 * All original values are saved and fully restored on unload.
 *
 * CONFIG:  Edit kpms/disabler/tweaks.h to add/remove int tweaks.
 * SPECIAL: unsigned long symbols are handled in the SPECIAL CASES section below.
 *
 * Changelog:
 *   v1.1 - pr_warn for key messages so dmesg shows them even with restricted
 *          printk level.  Print per-symbol OK/MISSING so you can verify exactly
 *          which tweaks are active.  console_loglevel moved to tweaks.h with a
 *          comment — it is Samsung-specific and may not be in kallsyms.
 *
 * Verify it ran:  su -c "dmesg | grep EK-DISABLE"
 * Check values:   su -c "cat /proc/sys/kernel/panic_on_oops"
 */

#include <compiler.h>
#include <kpmodule.h>
#include <kputils.h>
#include <linux/printk.h>
#include <linux/string.h>
#include <kallsyms.h>

KPM_NAME("disabler");
KPM_VERSION("1.1.0");
KPM_LICENSE("GPL v2");
KPM_AUTHOR("ExtremeKernel");
KPM_DESCRIPTION("Runtime kernel debug disabler. Edit tweaks.h to configure. Full restore on unload.");

/* ================================================================
 * INT TWEAKS — generated from tweaks.h
 * ================================================================ */
typedef struct {
    const char *sym;
    int         target;
    int        *ptr;
    int         orig;
} int_tweak_t;

static int_tweak_t int_tweaks[] = {
#define EK_DISABLE(sym, val)  { sym, val, 0, 0 },
#include "tweaks.h"
#undef EK_DISABLE
};

#define N_INT_TWEAKS  (sizeof(int_tweaks) / sizeof(int_tweaks[0]))

/* ================================================================
 * SPECIAL CASES — unsigned long symbols (add more here as needed)
 * ================================================================ */
typedef struct {
    const char    *sym;
    unsigned long  target;
    unsigned long *ptr;
    unsigned long  orig;
} long_tweak_t;

static long_tweak_t long_tweaks[] = {
    /* Hung task watchdog: fires if a task sleeps in D-state > N seconds.
     * Setting to 0 disables it. Prevents false-positive debug panics. */
    { "sysctl_hung_task_timeout_secs", 0, 0, 120 },
};

#define N_LONG_TWEAKS (sizeof(long_tweaks) / sizeof(long_tweaks[0]))

/* ================================================================
 * INIT
 * ================================================================ */
static long disabler_init(const char *args, const char *event, void *__user reserved)
{
    unsigned int i;
    unsigned int applied_int  = 0;
    unsigned int applied_long = 0;

    pr_warn("[EK-DISABLE] disabler v1.1 loading (%zu int + %zu long tweaks)...\n",
            N_INT_TWEAKS, N_LONG_TWEAKS);

    /* Resolve and apply int tweaks */
    for (i = 0; i < N_INT_TWEAKS; i++) {
        int_tweak_t *t = &int_tweaks[i];
        t->ptr = (int *)kallsyms_lookup_name(t->sym);
        if (!t->ptr) {
            pr_warn("[EK-DISABLE] %-40s MISSING (skipped)\n", t->sym);
            continue;
        }
        t->orig = *t->ptr;
        *t->ptr = t->target;
        pr_warn("[EK-DISABLE] %-40s %d -> %d OK\n", t->sym, t->orig, t->target);
        applied_int++;
    }

    /* Resolve and apply unsigned long tweaks */
    for (i = 0; i < N_LONG_TWEAKS; i++) {
        long_tweak_t *t = &long_tweaks[i];
        t->ptr = (unsigned long *)kallsyms_lookup_name(t->sym);
        if (!t->ptr) {
            pr_warn("[EK-DISABLE] %-40s MISSING (skipped)\n", t->sym);
            continue;
        }
        t->orig = *t->ptr;
        *t->ptr = t->target;
        pr_warn("[EK-DISABLE] %-40s %lu -> %lu OK\n", t->sym, t->orig, t->target);
        applied_long++;
    }

    pr_warn("[EK-DISABLE] LOADED v1.1: %u/%zu int + %u/%zu long tweaks applied.\n",
            applied_int, N_INT_TWEAKS, applied_long, N_LONG_TWEAKS);
    return 0;
}

/* ================================================================
 * CTL0 — status shown when you tap the gear icon in KSU-Next
 * ================================================================ */
static long disabler_ctl0(const char *args, char *__user out_msg, int outlen)
{
    const char *msg =
        "disabler v1.1 active\n"
        "Check dmesg for per-symbol OK/MISSING report:\n"
        "  su -c \"dmesg | grep EK-DISABLE\"\n"
        "\nExpected (if symbols found):\n"
        "  panic_on_oops        : 0\n"
        "  softlockup_panic     : 0\n"
        "  hardlockup_panic     : 0\n"
        "  sched_schedstats     : 0\n"
        "  hung_task_timeout    : 0 (disabled)\n";
    int len = 0;
    while (msg[len]) len++;
    compat_copy_to_user(out_msg, msg, len + 1);
    return 0;
}

/* ================================================================
 * EXIT — full restore
 * ================================================================ */
static long disabler_exit(void *__user reserved)
{
    unsigned int i;

    pr_warn("[EK-DISABLE] unloading — restoring all originals...\n");

    for (i = 0; i < N_INT_TWEAKS; i++) {
        int_tweak_t *t = &int_tweaks[i];
        if (t->ptr) {
            *t->ptr = t->orig;
            pr_warn("[EK-DISABLE] restored %-36s -> %d\n", t->sym, t->orig);
        }
    }

    for (i = 0; i < N_LONG_TWEAKS; i++) {
        long_tweak_t *t = &long_tweaks[i];
        if (t->ptr) {
            *t->ptr = t->orig;
            pr_warn("[EK-DISABLE] restored %-36s -> %lu\n", t->sym, t->orig);
        }
    }

    pr_warn("[EK-DISABLE] UNLOADED cleanly.\n");
    return 0;
}

KPM_INIT(disabler_init);
KPM_CTL0(disabler_ctl0);
KPM_EXIT(disabler_exit);
