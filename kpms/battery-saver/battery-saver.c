/* SPDX-License-Identifier: GPL-2.0 */
/*
 * battery-saver.kpm  v1.1  -  ExtremeKernel Battery Saver
 * Samsung Galaxy S20 Ultra (d2s, Exynos 9825), kernel 4.14
 *
 * Safe, purely runtime kernel tunables that reduce CPU/disk wake-ups.
 * All values reset on reboot. Has zero effect if not loaded.
 * Does NOT touch charging current, voltage, or thermal limits.
 *
 * v1.1: KPatch dynamic loading fix - snprintf resolved at runtime via
 *       kallsyms_lookup_name to avoid unresolvable UND ELF symbol.
 *       (KPatch exports snprintf as "kf_snprintf", not "snprintf".)
 */

#include <compiler.h>
#include <kpmodule.h>
#include <kputils.h>
#include <linux/printk.h>
#include <linux/string.h>
#include <kallsyms.h>

KPM_NAME("battery-saver");
KPM_VERSION("1.1.0");
KPM_LICENSE("GPL v2");
KPM_AUTHOR("ExtremeKernel");
KPM_DESCRIPTION("Battery-saver: reduce CPU/disk wake-ups for d2s (Exynos 9825)");

static unsigned int *p_writeback_interval  = 0;
static unsigned int *p_expire_interval     = 0;
static int          *p_laptop_mode         = 0;
static unsigned int *p_sched_energy_aware  = 0;

/* Saved originals so exit() can restore them */
static unsigned int  orig_writeback_interval = 500;
static unsigned int  orig_expire_interval    = 3000;
static int           orig_laptop_mode        = 0;
static unsigned int  orig_sched_energy_aware = 0;

/*
 * snprintf is NOT exported by KPatch under the bare name "snprintf".
 * KPatch wraps it as kf_snprintf and exports "kf_snprintf".  If the KPM
 * has a direct UND symbol for "snprintf", simplify_symbols() returns
 * ENOENT and loading fails.  Resolve it at runtime instead.
 */
static int (*kpm_snprintf)(char *buf, size_t size, const char *fmt, ...) = 0;

static long batt_saver_init(const char *args, const char *event, void *__user reserved)
{
    pr_info("[ek-battery] Loading ExtremeKernel Battery Saver v1.1...\n");

    /* Resolve snprintf at runtime - avoids unresolvable UND symbol */
    kpm_snprintf = (typeof(kpm_snprintf))kallsyms_lookup_name("snprintf");
    if (!kpm_snprintf)
        pr_warn("[ek-battery] snprintf not found via kallsyms - ctl0 status disabled\n");

    /*
     * Resolve kernel symbols. Names from mm/page-writeback.c (4.14).
     * Gracefully skip anything Samsung renamed or removed.
     */
    p_writeback_interval = (unsigned int *)kallsyms_lookup_name("dirty_writeback_interval");
    p_expire_interval    = (unsigned int *)kallsyms_lookup_name("dirty_expire_interval");
    p_laptop_mode        = (int *)kallsyms_lookup_name("laptop_mode");
    p_sched_energy_aware = (unsigned int *)kallsyms_lookup_name("sysctl_sched_energy_aware");

    if (p_writeback_interval) {
        orig_writeback_interval = *p_writeback_interval;
        pr_info("[ek-battery] dirty_writeback_interval:  %u -> 1500 cs (15s)\n", orig_writeback_interval);
        *p_writeback_interval = 1500;
    } else {
        pr_warn("[ek-battery] dirty_writeback_interval not found - skipping\n");
    }

    if (p_expire_interval) {
        orig_expire_interval = *p_expire_interval;
        pr_info("[ek-battery] dirty_expire_interval:     %u -> 6000 cs (60s)\n", orig_expire_interval);
        *p_expire_interval = 6000;
    } else {
        pr_warn("[ek-battery] dirty_expire_interval not found - skipping\n");
    }

    if (p_laptop_mode) {
        orig_laptop_mode = *p_laptop_mode;
        pr_info("[ek-battery] laptop_mode:               %d -> 5\n", orig_laptop_mode);
        *p_laptop_mode = 5;
    } else {
        pr_warn("[ek-battery] laptop_mode not found - skipping\n");
    }

    if (p_sched_energy_aware) {
        orig_sched_energy_aware = *p_sched_energy_aware;
        pr_info("[ek-battery] sysctl_sched_energy_aware: %u -> 1\n", orig_sched_energy_aware);
        *p_sched_energy_aware = 1;
    } else {
        pr_warn("[ek-battery] sysctl_sched_energy_aware not found - skipping\n");
    }

    pr_info("[ek-battery] Battery saver active.\n");
    return 0;
}

static long batt_saver_ctl0(const char *args, char *__user out_msg, int outlen)
{
    char buf[512];
    int n = 0;
    if (kpm_snprintf) {
        n += kpm_snprintf(buf + n, sizeof(buf) - n,
            "battery-saver v1.1 live values:\n"
            "  dirty_writeback_interval  = %u cs (orig: %u)\n"
            "  dirty_expire_interval     = %u cs (orig: %u)\n"
            "  laptop_mode               = %d (orig: %d)\n"
            "  sched_energy_aware        = %u (orig: %u)\n",
            p_writeback_interval ? *p_writeback_interval : 0, orig_writeback_interval,
            p_expire_interval    ? *p_expire_interval    : 0, orig_expire_interval,
            p_laptop_mode        ? *p_laptop_mode        : 0, orig_laptop_mode,
            p_sched_energy_aware ? *p_sched_energy_aware : 0, orig_sched_energy_aware
        );
    } else {
        /* snprintf unavailable - return minimal status */
        buf[0] = 'O'; buf[1] = 'K'; buf[2] = '\n'; buf[3] = '\0';
        n = 3;
    }
    compat_copy_to_user(out_msg, buf, n + 1);
    return 0;
}

static long batt_saver_exit(void *__user reserved)
{
    pr_info("[ek-battery] Unloading — restoring original values...\n");

    if (p_writeback_interval) *p_writeback_interval = orig_writeback_interval;
    if (p_expire_interval)    *p_expire_interval    = orig_expire_interval;
    if (p_laptop_mode)        *p_laptop_mode        = orig_laptop_mode;
    if (p_sched_energy_aware) *p_sched_energy_aware = orig_sched_energy_aware;

    pr_info("[ek-battery] All values restored. Unloaded cleanly.\n");
    return 0;
}

KPM_INIT(batt_saver_init);
KPM_CTL0(batt_saver_ctl0);
KPM_EXIT(batt_saver_exit);
