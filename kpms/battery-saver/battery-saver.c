/* SPDX-License-Identifier: GPL-2.0 */
/*
 * battery-saver.kpm  v2.3  -  ExtremeKernel Battery Saver
 * Samsung Galaxy S20 Ultra (d2s, Exynos 9825), kernel 4.14
 *
 * All values saved on load and fully restored on unload.
 * Confirmed working symbols on Samsung 4.14 d2s.
 *
 * Applied tweaks:
 *   dirty_writeback_interval  -> 3000 cs (30s)   disk wakes up 6x less
 *   dirty_expire_interval     -> 9000 cs (90s)   data batches in RAM longer
 *   laptop_mode               -> 5               batch all disk I/O
 *   vm_swappiness             -> 20              Samsung default is 160 (!!)
 *   vfs_cache_pressure        -> 50              file cache stays in RAM longer
 *   sched_migration_cost_ns   -> 5000000         tasks stay on same cluster
 *
 * Note: sysctl_tcp_window_scaling / sysctl_tcp_sack / tcp_timestamps are NOT
 * exported by Samsung 4.14 — removed, Android defaults are fine.
 */

#include <compiler.h>
#include <kpmodule.h>
#include <kputils.h>
#include <linux/printk.h>
#include <linux/string.h>
#include <kallsyms.h>

KPM_NAME("battery-saver");
KPM_VERSION("2.3.0");
KPM_LICENSE("GPL v2");
KPM_AUTHOR("ExtremeKernel");
KPM_DESCRIPTION("Max battery life: I/O batching + low swappiness + cache tuning. Full restore on unload.");

static unsigned int *p_writeback_interval = 0;
static unsigned int *p_expire_interval    = 0;
static int          *p_laptop_mode        = 0;
static int          *p_swappiness         = 0;
static int          *p_cache_pressure     = 0;
static unsigned int *p_migration_cost     = 0;

static unsigned int  orig_writeback_interval = 500;
static unsigned int  orig_expire_interval    = 3000;
static int           orig_laptop_mode        = 0;
static int           orig_swappiness         = 60;
static int           orig_cache_pressure     = 100;
static unsigned int  orig_migration_cost     = 500000;

#define RESOLVE(ptr, type, name) \
    ptr = (type *)kallsyms_lookup_name(name); \
    pr_info("[EK-BATT] " name ": %s\n", ptr ? "ok" : "missing")

#define APPLY(ptr, orig, val) \
    if (ptr) { orig = *ptr; *ptr = (val); }

#define RESTORE(ptr, orig) \
    if (ptr) { *ptr = (orig); }

static long batt_saver_init(const char *args, const char *event, void *__user reserved)
{
    pr_info("[EK-BATT] battery-saver v2.3 loading...\n");

    RESOLVE(p_writeback_interval, unsigned int, "dirty_writeback_interval");
    RESOLVE(p_expire_interval,    unsigned int, "dirty_expire_interval");
    RESOLVE(p_laptop_mode,        int,          "laptop_mode");
    RESOLVE(p_swappiness,         int,          "vm_swappiness");
    RESOLVE(p_cache_pressure,     int,          "sysctl_vfs_cache_pressure");
    RESOLVE(p_migration_cost,     unsigned int, "sysctl_sched_migration_cost");

    APPLY(p_writeback_interval, orig_writeback_interval, 3000);
    APPLY(p_expire_interval,    orig_expire_interval,    9000);
    APPLY(p_laptop_mode,        orig_laptop_mode,        5);
    APPLY(p_swappiness,         orig_swappiness,         20);
    APPLY(p_cache_pressure,     orig_cache_pressure,     50);
    APPLY(p_migration_cost,     orig_migration_cost,     5000000);

    pr_info("[EK-BATT] loaded. swappiness %d->20, writeback %d->3000, cache_pressure %d->50\n",
            orig_swappiness, orig_writeback_interval, orig_cache_pressure);
    return 0;
}

static long batt_saver_ctl0(const char *args, char *__user out_msg, int outlen)
{
    const char *msg =
        "battery-saver v2.3 active\n"
        "dirty_writeback : 3000 cs\n"
        "dirty_expire    : 9000 cs\n"
        "laptop_mode     : 5\n"
        "swappiness      : 20 (stock Samsung: 160)\n"
        "cache_pressure  : 50\n"
        "migration_cost  : 5000000 ns\n";
    int len = 0;
    while (msg[len]) len++;
    compat_copy_to_user(out_msg, msg, len + 1);
    return 0;
}

static long batt_saver_exit(void *__user reserved)
{
    pr_info("[EK-BATT] unloading — restoring originals...\n");

    RESTORE(p_writeback_interval, orig_writeback_interval);
    RESTORE(p_expire_interval,    orig_expire_interval);
    RESTORE(p_laptop_mode,        orig_laptop_mode);
    RESTORE(p_swappiness,         orig_swappiness);
    RESTORE(p_cache_pressure,     orig_cache_pressure);
    RESTORE(p_migration_cost,     orig_migration_cost);

    pr_info("[EK-BATT] unloaded cleanly.\n");
    return 0;
}

KPM_INIT(batt_saver_init);
KPM_CTL0(batt_saver_ctl0);
KPM_EXIT(batt_saver_exit);
