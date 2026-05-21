/* SPDX-License-Identifier: GPL-2.0 */
/*
 * extreme-tweaker.kpm  -  ExtremeKernel Performance Tweaker
 * Samsung Galaxy S20 Ultra (d2s, Exynos 9825), kernel 4.14
 *
 * Applies optimised sysctl values at boot via kallsyms_lookup_name.
 * No kernel recompile needed. All values reset on reboot.
 *
 * Tweaks applied on load:
 *   vm.swappiness                    -> 10  (more RAM cache, less swap)
 *   vm.dirty_ratio                   -> 20  (sane write-back ceiling)
 *   vm.dirty_background_ratio        ->  5  (earlier background flush)
 *   net.ipv4.tcp_fastopen            ->  3  (TFO client + server)
 *   net.ipv4.tcp_slow_start_after_idle -> 0 (no cwnd reset on idle)
 *   net.ipv4.tcp_tw_reuse            ->  1  (faster socket recycling)
 */

#include <compiler.h>
#include <kpmodule.h>
#include <kputils.h>
#include <linux/printk.h>
#include <linux/string.h>
#include <kallsyms.h>

KPM_NAME("extreme-tweaker");
KPM_VERSION("1.0.0");
KPM_LICENSE("GPL v2");
KPM_AUTHOR("ExtremeKernel");
KPM_DESCRIPTION("Boot-time sysctl performance tweaks for Exynos 9825");

static int          *p_swappiness          = 0;
static int          *p_dirty_ratio         = 0;
static int          *p_dirty_bg_ratio      = 0;
static unsigned int *p_tcp_fastopen        = 0;
static int          *p_tcp_slow_start_idle = 0;
static int          *p_tcp_tw_reuse        = 0;

static long tweaker_init(const char *args, const char *event, void *__user reserved)
{
    pr_info("[ek-tweaker] Loading ExtremeKernel Tweaker v1.0...\n");

    p_swappiness          = (int *)kallsyms_lookup_name("vm_swappiness");
    p_dirty_ratio         = (int *)kallsyms_lookup_name("vm_dirty_ratio");
    p_dirty_bg_ratio      = (int *)kallsyms_lookup_name("vm_dirty_background_ratio");
    p_tcp_fastopen        = (unsigned int *)kallsyms_lookup_name("sysctl_tcp_fastopen");
    p_tcp_slow_start_idle = (int *)kallsyms_lookup_name("tcp_slow_start_after_idle");
    p_tcp_tw_reuse        = (int *)kallsyms_lookup_name("tcp_tw_reuse");

    if (p_swappiness) {
        pr_info("[ek-tweaker] vm.swappiness:             %d -> 10\n", *p_swappiness);
        *p_swappiness = 10;
    }
    if (p_dirty_ratio) {
        pr_info("[ek-tweaker] vm.dirty_ratio:            %d -> 20\n", *p_dirty_ratio);
        *p_dirty_ratio = 20;
    }
    if (p_dirty_bg_ratio) {
        pr_info("[ek-tweaker] vm.dirty_bg_ratio:         %d -> 5\n", *p_dirty_bg_ratio);
        *p_dirty_bg_ratio = 5;
    }
    if (p_tcp_fastopen) {
        pr_info("[ek-tweaker] tcp_fastopen:              %u -> 3\n", *p_tcp_fastopen);
        *p_tcp_fastopen = 3;
    }
    if (p_tcp_slow_start_idle) {
        pr_info("[ek-tweaker] tcp_slow_start_after_idle: %d -> 0\n", *p_tcp_slow_start_idle);
        *p_tcp_slow_start_idle = 0;
    }
    if (p_tcp_tw_reuse) {
        pr_info("[ek-tweaker] tcp_tw_reuse:              %d -> 1\n", *p_tcp_tw_reuse);
        *p_tcp_tw_reuse = 1;
    }

    pr_info("[ek-tweaker] All tweaks applied.\n");
    return 0;
}

static long tweaker_ctl0(const char *args, char *__user out_msg, int outlen)
{
    char buf[512];
    int n = 0;
    n += snprintf(buf + n, sizeof(buf) - n,
        "extreme-tweaker v1.0:\n"
        "  vm.swappiness              = %d\n"
        "  vm.dirty_ratio             = %d\n"
        "  vm.dirty_bg_ratio          = %d\n"
        "  tcp_fastopen               = %u\n"
        "  tcp_slow_start_after_idle  = %d\n"
        "  tcp_tw_reuse               = %d\n",
        p_swappiness          ? *p_swappiness          : -1,
        p_dirty_ratio         ? *p_dirty_ratio         : -1,
        p_dirty_bg_ratio      ? *p_dirty_bg_ratio      : -1,
        p_tcp_fastopen        ? *p_tcp_fastopen        :  0,
        p_tcp_slow_start_idle ? *p_tcp_slow_start_idle : -1,
        p_tcp_tw_reuse        ? *p_tcp_tw_reuse        : -1
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
