/* SPDX-License-Identifier: GPL-2.0 */
/*
 * battery-saver.kpm  v2.2-diag  -  ExtremeKernel Battery Saver (diagnostic)
 * Samsung Galaxy S20 Ultra (d2s, Exynos 9825), kernel 4.14
 *
 * Diagnostic build: heavy logging to confirm init runs and symbol resolution.
 * Check: su -c "dmesg | grep EK-BATT"
 */

#include <compiler.h>
#include <kpmodule.h>
#include <kputils.h>
#include <linux/printk.h>
#include <linux/string.h>
#include <kallsyms.h>

KPM_NAME("battery-saver");
KPM_VERSION("2.2.0");
KPM_LICENSE("GPL v2");
KPM_AUTHOR("ExtremeKernel");
KPM_DESCRIPTION("Max battery life: I/O batching + low swappiness. Full restore on unload.");

static unsigned int  *p_writeback_interval  = 0;
static unsigned int  *p_expire_interval     = 0;
static int           *p_laptop_mode         = 0;
static int           *p_swappiness          = 0;
static unsigned int  *p_migration_cost      = 0;

static unsigned int   orig_writeback_interval  = 500;
static unsigned int   orig_expire_interval     = 3000;
static int            orig_laptop_mode         = 0;
static int            orig_swappiness          = 60;
static unsigned int   orig_migration_cost      = 500000;

static long batt_saver_init(const char *args, const char *event, void *__user reserved)
{
    /* === LINE 1: proof init ran at all === */
    pr_err("[EK-BATT] >>> INIT STARTED v2.2 <<<\n");

    /* Resolve symbols and log each one explicitly */
    p_writeback_interval = (unsigned int *)kallsyms_lookup_name("dirty_writeback_interval");
    pr_err("[EK-BATT] dirty_writeback_interval addr: %p\n", p_writeback_interval);

    p_expire_interval = (unsigned int *)kallsyms_lookup_name("dirty_expire_interval");
    pr_err("[EK-BATT] dirty_expire_interval addr:    %p\n", p_expire_interval);

    p_laptop_mode = (int *)kallsyms_lookup_name("laptop_mode");
    pr_err("[EK-BATT] laptop_mode addr:               %p\n", p_laptop_mode);

    p_swappiness = (int *)kallsyms_lookup_name("vm_swappiness");
    pr_err("[EK-BATT] vm_swappiness addr:             %p\n", p_swappiness);

    p_migration_cost = (unsigned int *)kallsyms_lookup_name("sysctl_sched_migration_cost");
    pr_err("[EK-BATT] sched_migration_cost addr:      %p\n", p_migration_cost);

    /* Apply values */
    if (p_writeback_interval) {
        orig_writeback_interval = *p_writeback_interval;
        *p_writeback_interval = 3000;
        pr_err("[EK-BATT] dirty_writeback_interval: %u -> 3000\n", orig_writeback_interval);
    }
    if (p_expire_interval) {
        orig_expire_interval = *p_expire_interval;
        *p_expire_interval = 9000;
        pr_err("[EK-BATT] dirty_expire_interval:    %u -> 9000\n", orig_expire_interval);
    }
    if (p_laptop_mode) {
        orig_laptop_mode = *p_laptop_mode;
        *p_laptop_mode = 5;
        pr_err("[EK-BATT] laptop_mode:               %d -> 5\n", orig_laptop_mode);
    } else {
        pr_err("[EK-BATT] laptop_mode: NOT FOUND in kallsyms - Samsung removed it\n");
    }
    if (p_swappiness) {
        orig_swappiness = *p_swappiness;
        *p_swappiness = 20;
        pr_err("[EK-BATT] vm_swappiness:             %d -> 20\n", orig_swappiness);
    }
    if (p_migration_cost) {
        orig_migration_cost = *p_migration_cost;
        *p_migration_cost = 5000000;
        pr_err("[EK-BATT] sched_migration_cost_ns:   %u -> 5000000\n", orig_migration_cost);
    }

    pr_err("[EK-BATT] >>> INIT COMPLETE <<<\n");
    return 0;
}

static long batt_saver_ctl0(const char *args, char *__user out_msg, int outlen)
{
    /* Simple status — no snprintf dependency */
    const char *msg = "battery-saver v2.2 loaded. Check: dmesg | grep EK-BATT\n";
    int len = 0;
    while (msg[len]) len++;
    compat_copy_to_user(out_msg, msg, len + 1);
    return 0;
}

static long batt_saver_exit(void *__user reserved)
{
    pr_err("[EK-BATT] >>> EXIT - restoring originals <<<\n");

    if (p_writeback_interval) { *p_writeback_interval = orig_writeback_interval; pr_err("[EK-BATT] dirty_writeback_interval restored: %u\n", orig_writeback_interval); }
    if (p_expire_interval)    { *p_expire_interval    = orig_expire_interval;    pr_err("[EK-BATT] dirty_expire_interval restored:    %u\n", orig_expire_interval); }
    if (p_laptop_mode)        { *p_laptop_mode        = orig_laptop_mode;        pr_err("[EK-BATT] laptop_mode restored:               %d\n", orig_laptop_mode); }
    if (p_swappiness)         { *p_swappiness         = orig_swappiness;         pr_err("[EK-BATT] vm_swappiness restored:              %d\n", orig_swappiness); }
    if (p_migration_cost)     { *p_migration_cost     = orig_migration_cost;     pr_err("[EK-BATT] sched_migration_cost_ns restored:    %u\n", orig_migration_cost); }

    pr_err("[EK-BATT] >>> EXIT COMPLETE <<<\n");
    return 0;
}

KPM_INIT(batt_saver_init);
KPM_CTL0(batt_saver_ctl0);
KPM_EXIT(batt_saver_exit);
