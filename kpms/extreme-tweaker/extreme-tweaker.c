/* SPDX-License-Identifier: GPL-2.0 */
/*
 * extreme-tweaker.kpm  v1.1  ExtremeKernel Performance Tweaker
 * Samsung Galaxy S20 Ultra (d2s, Exynos 9825), kernel 4.14
 *
 * Symbol names verified on-device via /proc/kallsyms:
 *   vm_swappiness                    confirmed
 *   vm_dirty_ratio                   confirmed
 *   vm_dirty_background_ratio        confirmed
 *   vm_dirty_bytes                   zero first (Samsung sets this; while non-zero, dirty_ratio is ignored)
 *   vm_dirty_background_bytes        zero first (same reason)
 *   sysctl_tcp_fastopen              confirmed
 *   sysctl_tcp_slow_start_after_idle confirmed (NOT tcp_slow_start_after_idle)
 *   tcp_tw_reuse                     NOT present on Samsung 4.14 - removed
 */

#include <compiler.h>
#include <kpmodule.h>
#include <kputils.h>
#include <linux/printk.h>
#include <linux/string.h>
#include <kallsyms.h>

KPM_NAME("extreme-tweaker");
KPM_VERSION("1.1.0");
KPM_LICENSE("GPL v2");
KPM_AUTHOR("ExtremeKernel");
KPM_DESCRIPTION("Performance sysctl tweaks for d2s Exynos 9825 - v1.1 corrected symbols");

static int           *p_swappiness          = 0;
static int           *p_dirty_ratio         = 0;
static int           *p_dirty_bg_ratio      = 0;
static unsigned long *p_dirty_bytes         = 0;
static unsigned long *p_dirty_bg_bytes      = 0;
static unsigned int  *p_tcp_fastopen        = 0;
static int           *p_tcp_slow_start_idle = 0;

static long tweaker_init(const char *args, const char *event, void *__user reserved)
{
    pr_info("[ek-tweaker] ExtremeKernel Performance Tweaker v1.1 loading...\n");

    p_swappiness          = (int *)kallsyms_lookup_name("vm_swappiness");
    p_dirty_ratio         = (int *)kallsyms_lookup_name("vm_dirty_ratio");
    p_dirty_bg_ratio      = (int *)kallsyms_lookup_name("vm_dirty_background_ratio");
    p_dirty_bytes         = (unsigned long *)kallsyms_lookup_name("vm_dirty_bytes");
    p_dirty_bg_bytes      = (unsigned long *)kallsyms_lookup_name("vm_dirty_background_bytes");
    p_tcp_fastopen        = (unsigned int *)kallsyms_lookup_name("sysctl_tcp_fastopen");
    p_tcp_slow_start_idle = (int *)kallsyms_lookup_name("sysctl_tcp_slow_start_after_idle");

    /* Log any symbols that failed to resolve so dmesg makes it obvious */
    if (!p_swappiness)          pr_err("[ek-tweaker] MISSING: vm_swappiness not in kallsyms\n");
    if (!p_dirty_ratio)         pr_err("[ek-tweaker] MISSING: vm_dirty_ratio not in kallsyms\n");
    if (!p_dirty_bg_ratio)      pr_err("[ek-tweaker] MISSING: vm_dirty_background_ratio not in kallsyms\n");
    if (!p_dirty_bytes)         pr_warn("[ek-tweaker] MISSING: vm_dirty_bytes not in kallsyms (skipping)\n");
    if (!p_dirty_bg_bytes)      pr_warn("[ek-tweaker] MISSING: vm_dirty_background_bytes not in kallsyms (skipping)\n");
    if (!p_tcp_fastopen)        pr_err("[ek-tweaker] MISSING: sysctl_tcp_fastopen not in kallsyms\n");
    if (!p_tcp_slow_start_idle) pr_err("[ek-tweaker] MISSING: sysctl_tcp_slow_start_after_idle not in kallsyms\n");

    /* Samsung sets vm_dirty_bytes != 0 which makes vm_dirty_ratio ignored.
     * Zero it out first so our ratio settings actually take effect. */
    if (p_dirty_bytes && *p_dirty_bytes != 0) {
        pr_info("[ek-tweaker] vm_dirty_bytes:            %lu -> 0\n", *p_dirty_bytes);
        *p_dirty_bytes = 0;
    }
    if (p_dirty_bg_bytes && *p_dirty_bg_bytes != 0) {
        pr_info("[ek-tweaker] vm_dirty_background_bytes: %lu -> 0\n", *p_dirty_bg_bytes);
        *p_dirty_bg_bytes = 0;
    }

    if (p_swappiness) {
        pr_info("[ek-tweaker] vm_swappiness:             %d -> 10\n", *p_swappiness);
        *p_swappiness = 10;
    }
    if (p_dirty_ratio) {
        pr_info("[ek-tweaker] vm_dirty_ratio:            %d -> 20\n", *p_dirty_ratio);
        *p_dirty_ratio = 20;
    }
    if (p_dirty_bg_ratio) {
        pr_info("[ek-tweaker] vm_dirty_bg_ratio:         %d -> 5\n", *p_dirty_bg_ratio);
        *p_dirty_bg_ratio = 5;
    }
    if (p_tcp_fastopen) {
        pr_info("[ek-tweaker] sysctl_tcp_fastopen:       %u -> 3\n", *p_tcp_fastopen);
        *p_tcp_fastopen = 3;
    }
    if (p_tcp_slow_start_idle) {
        pr_info("[ek-tweaker] sysctl_tcp_slow_start:     %d -> 0\n", *p_tcp_slow_start_idle);
        *p_tcp_slow_start_idle = 0;
    }

    pr_info("[ek-tweaker] All tweaks applied.\n");
    return 0;
}

static long tweaker_ctl0(const char *args, char *__user out_msg, int outlen)
{
    char buf[640];
    int n = 0;
    n += snprintf(buf + n, sizeof(buf) - n,
        "extreme-tweaker v1.1:\n"
        "  vm_swappiness              = %d\n"
        "  vm_dirty_bytes             = %lu\n"
        "  vm_dirty_background_bytes  = %lu\n"
        "  vm_dirty_ratio             = %d\n"
        "  vm_dirty_background_ratio  = %d\n"
        "  sysctl_tcp_fastopen        = %u\n"
        "  sysctl_tcp_slow_start      = %d\n",
        p_swappiness          ? *p_swappiness          : -1,
        p_dirty_bytes         ? *p_dirty_bytes         :  0,
        p_dirty_bg_bytes      ? *p_dirty_bg_bytes      :  0,
        p_dirty_ratio         ? *p_dirty_ratio         : -1,
        p_dirty_bg_ratio      ? *p_dirty_bg_ratio      : -1,
        p_tcp_fastopen        ? *p_tcp_fastopen        :  0,
        p_tcp_slow_start_idle ? *p_tcp_slow_start_idle : -1
    );
    compat_copy_to_user(out_msg, buf, n + 1);
    return 0;
}

static long tweaker_exit(void *__user reserved)
{
    pr_info("[ek-tweaker] Unloaded.\n");
    return 0;
}

KPM_INIT(tweaker_init);
KPM_CTL0(tweaker_ctl0);
KPM_EXIT(tweaker_exit);
