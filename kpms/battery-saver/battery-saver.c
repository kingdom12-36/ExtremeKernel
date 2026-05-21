/* SPDX-License-Identifier: GPL-2.0 */
/*
 * battery-saver.kpm  -  ExtremeKernel Battery Saver
 * Samsung Galaxy S20 Ultra (d2s, Exynos 9825), kernel 4.14
 *
 * Safe, purely runtime kernel tunables that reduce CPU/disk wake-ups.
 * All values reset on reboot. Has zero effect if not loaded.
 * Does NOT touch charging current, voltage, or thermal limits.
 *
 * What it tunes:
 *
 * dirty_writeback_interval   500  -> 1500 centisecs
 *   The kernel wakes a writeback thread every 500cs (5s) to flush dirty pages.
 *   Tripling this to 15s means 3x fewer timer wake-ups = 3x less idle wakeup
 *   from disk housekeeping.  Safe: data still flushed before dirty_expire hits.
 *
 * dirty_expire_interval      3000 -> 6000 centisecs
 *   How old a dirty page must be before the kernel considers it expired.
 *   Doubling this lets the CPU stay asleep longer between dirty-page sweeps.
 *   Safe: no data loss risk on modern eMMC/UFS with battery-backed write cache.
 *
 * laptop_mode                0    -> 5
 *   Clusters disk writes together so the storage controller can stay in a low-
 *   power state for longer.  Standard Linux power-saving knob, widely used on
 *   mobile/embedded.  Value 5 is the typical aggressive-but-safe setting.
 *
 * sched_energy_aware         *    -> 1
 *   Energy-Aware Scheduling: the scheduler prefers efficient little cores for
 *   light tasks instead of waking big cores unnecessarily.  Samsung 4.14 EAS
 *   is already partially active; this knob ensures it is fully on.
 *
 * Symbols verified strategy: use kallsyms_lookup_name at runtime, skip
 * gracefully if any symbol is absent (Samsung may rename or remove some).
 */

#include <compiler.h>
#include <kpmodule.h>
#include <kputils.h>
#include <linux/printk.h>
#include <linux/string.h>
#include <kallsyms.h>

KPM_NAME("battery-saver");
KPM_VERSION("1.0.0");
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

static long batt_saver_init(const char *args, const char *event, void *__user reserved)
{
    pr_info("[ek-battery] Loading ExtremeKernel Battery Saver v1.0...\n");

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
    n += snprintf(buf + n, sizeof(buf) - n,
        "battery-saver v1.0 live values:\n"
        "  dirty_writeback_interval  = %u cs (orig: %u)\n"
        "  dirty_expire_interval     = %u cs (orig: %u)\n"
        "  laptop_mode               = %d (orig: %d)\n"
        "  sched_energy_aware        = %u (orig: %u)\n",
        p_writeback_interval ? *p_writeback_interval : 0, orig_writeback_interval,
        p_expire_interval    ? *p_expire_interval    : 0, orig_expire_interval,
        p_laptop_mode        ? *p_laptop_mode        : 0, orig_laptop_mode,
        p_sched_energy_aware ? *p_sched_energy_aware : 0, orig_sched_energy_aware
    );
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
