/* SPDX-License-Identifier: GPL-2.0 */
/*
 * battery-saver.kpm  v2.1  -  ExtremeKernel Battery Saver
 * Samsung Galaxy S20 Ultra (d2s, Exynos 9825), kernel 4.14
 *
 * Purely runtime kernel tunables for maximum battery life.
 * All values are saved on load and fully restored on unload.
 * Has zero permanent effect — load to enable, unload to undo everything.
 *
 * Tweaks applied:
 *   dirty_writeback_interval  : 5s  -> 30s  (less frequent disk wake-ups)
 *   dirty_expire_interval     : 30s -> 90s  (data stays in RAM longer)
 *   laptop_mode               : 0   -> 5    (batch all I/O, let disk idle)
 *   vm_swappiness             : *   -> 20   (stay in RAM, avoid swap I/O)
 *   sched_migration_cost_ns   : *   -> 5ms  (fewer inter-cluster task moves)
 *
 * v2.1: removed sched_energy_aware — symbol not present on Samsung 4.14 d2s.
 */

#include <compiler.h>
#include <kpmodule.h>
#include <kputils.h>
#include <linux/printk.h>
#include <linux/string.h>
#include <kallsyms.h>

KPM_NAME("battery-saver");
KPM_VERSION("2.1.0");
KPM_LICENSE("GPL v2");
KPM_AUTHOR("ExtremeKernel");
KPM_DESCRIPTION("Max battery life: I/O batching + low swappiness. Full restore on unload.");

/* Pointers to kernel variables */
static unsigned int  *p_writeback_interval  = 0;
static unsigned int  *p_expire_interval     = 0;
static int           *p_laptop_mode         = 0;
static int           *p_swappiness          = 0;
static unsigned int  *p_migration_cost      = 0;

/* Saved originals — restored on exit */
static unsigned int   orig_writeback_interval  = 500;
static unsigned int   orig_expire_interval     = 3000;
static int            orig_laptop_mode         = 0;
static int            orig_swappiness          = 60;
static unsigned int   orig_migration_cost      = 500000;

/* Resolved at runtime to avoid unresolvable UND ELF symbol */
static int (*kpm_snprintf)(char *buf, size_t size, const char *fmt, ...) = 0;

static long batt_saver_init(const char *args, const char *event, void *__user reserved)
{
    pr_info("[ek-battery] Loading ExtremeKernel Battery Saver v2.1...\n");

    kpm_snprintf = (typeof(kpm_snprintf))kallsyms_lookup_name("snprintf");

    p_writeback_interval = (unsigned int *)kallsyms_lookup_name("dirty_writeback_interval");
    p_expire_interval    = (unsigned int *)kallsyms_lookup_name("dirty_expire_interval");
    p_laptop_mode        = (int *)kallsyms_lookup_name("laptop_mode");
    p_swappiness         = (int *)kallsyms_lookup_name("vm_swappiness");
    p_migration_cost     = (unsigned int *)kallsyms_lookup_name("sysctl_sched_migration_cost");

    if (p_writeback_interval) {
        orig_writeback_interval = *p_writeback_interval;
        *p_writeback_interval = 3000;
        pr_info("[ek-battery] dirty_writeback_interval: %u -> 3000 cs (30s)\n", orig_writeback_interval);
    } else {
        pr_warn("[ek-battery] dirty_writeback_interval not found\n");
    }

    if (p_expire_interval) {
        orig_expire_interval = *p_expire_interval;
        *p_expire_interval = 9000;
        pr_info("[ek-battery] dirty_expire_interval:    %u -> 9000 cs (90s)\n", orig_expire_interval);
    } else {
        pr_warn("[ek-battery] dirty_expire_interval not found\n");
    }

    if (p_laptop_mode) {
        orig_laptop_mode = *p_laptop_mode;
        *p_laptop_mode = 5;
        pr_info("[ek-battery] laptop_mode:               %d -> 5\n", orig_laptop_mode);
    } else {
        pr_warn("[ek-battery] laptop_mode not found\n");
    }

    if (p_swappiness) {
        orig_swappiness = *p_swappiness;
        *p_swappiness = 20;
        pr_info("[ek-battery] vm_swappiness:             %d -> 20\n", orig_swappiness);
    } else {
        pr_warn("[ek-battery] vm_swappiness not found\n");
    }

    if (p_migration_cost) {
        orig_migration_cost = *p_migration_cost;
        *p_migration_cost = 5000000;
        pr_info("[ek-battery] sched_migration_cost_ns:   %u -> 5000000\n", orig_migration_cost);
    } else {
        pr_warn("[ek-battery] sched_migration_cost not found\n");
    }

    pr_info("[ek-battery] Battery saver active. Unload to restore all originals.\n");
    return 0;
}

static long batt_saver_ctl0(const char *args, char *__user out_msg, int outlen)
{
    char buf[512];
    int n = 0;
    if (kpm_snprintf) {
        n = kpm_snprintf(buf, sizeof(buf),
            "battery-saver v2.1 — live values (orig):\n"
            "  dirty_writeback_interval  = %u cs (was %u)\n"
            "  dirty_expire_interval     = %u cs (was %u)\n"
            "  laptop_mode               = %d (was %d)\n"
            "  vm_swappiness             = %d (was %d)\n"
            "  sched_migration_cost_ns   = %u (was %u)\n",
            p_writeback_interval ? *p_writeback_interval : 0, orig_writeback_interval,
            p_expire_interval    ? *p_expire_interval    : 0, orig_expire_interval,
            p_laptop_mode        ? *p_laptop_mode        : 0, orig_laptop_mode,
            p_swappiness         ? *p_swappiness         : 0, orig_swappiness,
            p_migration_cost     ? *p_migration_cost     : 0, orig_migration_cost
        );
    } else {
        buf[0] = 'O'; buf[1] = 'K'; buf[2] = '\n'; buf[3] = '\0';
        n = 3;
    }
    compat_copy_to_user(out_msg, buf, n + 1);
    return 0;
}

static long batt_saver_exit(void *__user reserved)
{
    pr_info("[ek-battery] Unloading — restoring ALL original values...\n");

    if (p_writeback_interval) { *p_writeback_interval = orig_writeback_interval; pr_info("[ek-battery] dirty_writeback_interval restored: %u\n",  orig_writeback_interval); }
    if (p_expire_interval)    { *p_expire_interval    = orig_expire_interval;    pr_info("[ek-battery] dirty_expire_interval restored:    %u\n",  orig_expire_interval); }
    if (p_laptop_mode)        { *p_laptop_mode        = orig_laptop_mode;        pr_info("[ek-battery] laptop_mode restored:               %d\n",  orig_laptop_mode); }
    if (p_swappiness)         { *p_swappiness         = orig_swappiness;         pr_info("[ek-battery] vm_swappiness restored:              %d\n",  orig_swappiness); }
    if (p_migration_cost)     { *p_migration_cost     = orig_migration_cost;     pr_info("[ek-battery] sched_migration_cost_ns restored:    %u\n",  orig_migration_cost); }

    pr_info("[ek-battery] All values restored. Kernel back to original state.\n");
    return 0;
}

KPM_INIT(batt_saver_init);
KPM_CTL0(batt_saver_ctl0);
KPM_EXIT(batt_saver_exit);
