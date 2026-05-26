/* SPDX-License-Identifier: GPL-2.0 */
/*
 * battery-saver.kpm  v2.3  -  ExtremeKernel Battery Saver
 * Samsung Galaxy S20 Ultra (d2s, Exynos 9825), kernel 4.14
 *
 * All values saved on load and fully restored on unload.
 * Replaces the battery/RAM section of spoofer.sh — no conflicts.
 *
 * Tweaks:
 *   dirty_writeback_interval  -> 3000 cs (30s)   disk wakes up less
 *   dirty_expire_interval     -> 9000 cs (90s)   data stays in RAM longer
 *   laptop_mode               -> 5               batch all disk I/O
 *   vm_swappiness             -> 20              stay in RAM (script had 100 = BAD)
 *   vfs_cache_pressure        -> 50              keep file cache in RAM longer
 *   sched_migration_cost_ns   -> 5000000         fewer inter-cluster task moves
 *   tcp_window_scaling        -> 1               efficient TCP throughput
 *   tcp_sack                  -> 1               selective ack = fewer retransmits
 *   tcp_timestamps            -> 1               accurate RTT = fewer wakeups
 *
 * Remove from spoofer.sh after loading this KPM:
 *   sysctl -w vm.swappiness=...
 *   sysctl -w vm.vfs_cache_pressure=...
 *   sysctl -w net.ipv4.tcp_window_scaling=...
 *   sysctl -w net.ipv4.tcp_sack=...
 *   sysctl -w net.ipv4.tcp_timestamps=...
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
KPM_DESCRIPTION("Max battery life: I/O batching + low swappiness + TCP tuning. Full restore on unload.");

/* Pointers */
static unsigned int *p_writeback_interval  = 0;
static unsigned int *p_expire_interval     = 0;
static int          *p_laptop_mode         = 0;
static int          *p_swappiness          = 0;
static int          *p_cache_pressure      = 0;
static unsigned int *p_migration_cost      = 0;
static int          *p_tcp_window_scaling  = 0;
static int          *p_tcp_sack            = 0;
static int          *p_tcp_timestamps      = 0;

/* Saved originals */
static unsigned int  orig_writeback_interval = 500;
static unsigned int  orig_expire_interval    = 3000;
static int           orig_laptop_mode        = 0;
static int           orig_swappiness         = 60;
static int           orig_cache_pressure     = 100;
static unsigned int  orig_migration_cost     = 500000;
static int           orig_tcp_window_scaling = 1;
static int           orig_tcp_sack           = 1;
static int           orig_tcp_timestamps     = 1;

#define RESOLVE(ptr, type, name) \
    ptr = (type *)kallsyms_lookup_name(name); \
    pr_err("[EK-BATT] " name ": %s\n", ptr ? "found" : "NOT FOUND (skipping)")

static long batt_saver_init(const char *args, const char *event, void *__user reserved)
{
    pr_err("[EK-BATT] >>> battery-saver v2.3 INIT START <<<\n");

    RESOLVE(p_writeback_interval, unsigned int, "dirty_writeback_interval");
    RESOLVE(p_expire_interval,    unsigned int, "dirty_expire_interval");
    RESOLVE(p_laptop_mode,        int,          "laptop_mode");
    RESOLVE(p_swappiness,         int,          "vm_swappiness");
    RESOLVE(p_cache_pressure,     int,          "sysctl_vfs_cache_pressure");
    RESOLVE(p_migration_cost,     unsigned int, "sysctl_sched_migration_cost");
    RESOLVE(p_tcp_window_scaling, int,          "sysctl_tcp_window_scaling");
    RESOLVE(p_tcp_sack,           int,          "sysctl_tcp_sack");
    RESOLVE(p_tcp_timestamps,     int,          "tcp_timestamps");

    /* Apply — save original first, then set battery value */
#define APPLY(ptr, orig, val) \
    if (ptr) { orig = *ptr; *ptr = (val); \
        pr_err("[EK-BATT] applied: %d -> %d\n", (int)(orig), (int)(val)); }

    APPLY(p_writeback_interval, orig_writeback_interval, 3000);
    APPLY(p_expire_interval,    orig_expire_interval,    9000);
    APPLY(p_laptop_mode,        orig_laptop_mode,        5);
    APPLY(p_swappiness,         orig_swappiness,         20);
    APPLY(p_cache_pressure,     orig_cache_pressure,     50);
    APPLY(p_migration_cost,     orig_migration_cost,     5000000);
    APPLY(p_tcp_window_scaling, orig_tcp_window_scaling, 1);
    APPLY(p_tcp_sack,           orig_tcp_sack,           1);
    APPLY(p_tcp_timestamps,     orig_tcp_timestamps,     1);

    pr_err("[EK-BATT] >>> INIT COMPLETE — unload to restore all <<<\n");
    return 0;
}

static long batt_saver_ctl0(const char *args, char *__user out_msg, int outlen)
{
    const char *msg =
        "battery-saver v2.3 active\n"
        "dirty_writeback : 3000 cs\n"
        "dirty_expire    : 9000 cs\n"
        "laptop_mode     : 5\n"
        "swappiness      : 20\n"
        "cache_pressure  : 50\n"
        "tcp_win_scaling : 1\n"
        "tcp_sack        : 1\n"
        "tcp_timestamps  : 1\n";
    int len = 0;
    while (msg[len]) len++;
    compat_copy_to_user(out_msg, msg, len + 1);
    return 0;
}

static long batt_saver_exit(void *__user reserved)
{
    pr_err("[EK-BATT] >>> EXIT — restoring all originals <<<\n");

#define RESTORE(ptr, orig) \
    if (ptr) { *ptr = (orig); \
        pr_err("[EK-BATT] restored: %d\n", (int)(orig)); }

    RESTORE(p_writeback_interval, orig_writeback_interval);
    RESTORE(p_expire_interval,    orig_expire_interval);
    RESTORE(p_laptop_mode,        orig_laptop_mode);
    RESTORE(p_swappiness,         orig_swappiness);
    RESTORE(p_cache_pressure,     orig_cache_pressure);
    RESTORE(p_migration_cost,     orig_migration_cost);
    RESTORE(p_tcp_window_scaling, orig_tcp_window_scaling);
    RESTORE(p_tcp_sack,           orig_tcp_sack);
    RESTORE(p_tcp_timestamps,     orig_tcp_timestamps);

    pr_err("[EK-BATT] >>> EXIT COMPLETE — kernel back to stock <<<\n");
    return 0;
}

KPM_INIT(batt_saver_init);
KPM_CTL0(batt_saver_ctl0);
KPM_EXIT(batt_saver_exit);
