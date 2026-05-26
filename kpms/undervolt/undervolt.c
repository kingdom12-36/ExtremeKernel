/* SPDX-License-Identifier: GPL-2.0 */
/*
 * undervolt.kpm  v1.1  -  ExtremeKernel CPU/GPU Undervolt
 * Samsung Galaxy S20 Ultra (d2s, Exynos 9825), kernel 4.14
 *
 * Reduces each DVFS OPP voltage by a fixed offset (mV).
 * Writes to the same sysfs volt_table nodes that hKtweaks uses,
 * but from kernel space — no Android app needed, runs at boot.
 *
 * Original voltages are saved on load and fully restored on unload.
 *
 * ─── CONFIGURATION ─────────────────────────────────────────
 *   Edit the UV_OFFSET_* defines below, rebuild, reinstall.
 *   Safe start: -50 mV on CPUs, -25 mV on GPU.
 *   If you see random reboots, reduce offsets or unload module.
 *
 * ─── VERIFY ────────────────────────────────────────────────
 *   su -c "dmesg | grep EK-UV"
 *   su -c "cat /sys/devices/system/cpu/cpufreq/mp-cpufreq/cluster0_volt_table"
 *
 * Changelog:
 *   v1.1 - Complete rewrite. Same style as battery-saver / extreme-tweaker.
 *          No large stack frames, no complex structs. Static buffers only.
 *          Saves raw volt_table text on load; restores verbatim on unload.
 */

#include <compiler.h>
#include <kpmodule.h>
#include <kputils.h>
#include <linux/printk.h>
#include <linux/string.h>
#include <kallsyms.h>

KPM_NAME("undervolt");
KPM_VERSION("1.1.0");
KPM_LICENSE("GPL v2");
KPM_AUTHOR("ExtremeKernel");
KPM_DESCRIPTION("CPU/GPU undervolt for d2s Exynos 9825. Edit UV_OFFSET_* defines, rebuild. Full restore on unload.");

/* ═══════════════════════════════════════════════════════════
 * USER CONFIG — edit and rebuild
 * ═══════════════════════════════════════════════════════════ */
#define UV_OFFSET_CL0_UV   50000UL   /* little CPU cluster : -50 mV */
#define UV_OFFSET_CL1_UV   50000UL   /* big   CPU cluster  : -50 mV */
#define UV_OFFSET_GPU_UV   25000UL   /* GPU (Mali-G76)     : -25 mV */
#define UV_FLOOR_UV       550000UL   /* never go below 0.55 V        */

/* ═══════════════════════════════════════════════════════════
 * SYSFS PATHS
 * ═══════════════════════════════════════════════════════════ */
#define PATH_CL0   "/sys/devices/system/cpu/cpufreq/mp-cpufreq/cluster0_volt_table"
#define PATH_CL1   "/sys/devices/system/cpu/cpufreq/mp-cpufreq/cluster1_volt_table"
#define PATH_GPU_A "/sys/devices/platform/11500000.mali/volt_table"
#define PATH_GPU_B "/sys/kernel/gpu/gpu_volt_table"

/* ═══════════════════════════════════════════════════════════
 * KERNEL FUNCTION POINTERS — same pattern as extreme-tweaker
 * ═══════════════════════════════════════════════════════════ */
typedef struct file *(*fn_filp_open_t)(const char *, int, umode_t);
typedef int          (*fn_filp_close_t)(struct file *, void *);
typedef ssize_t      (*fn_kernel_read_t)(struct file *, void *, size_t, loff_t *);
typedef ssize_t      (*fn_kernel_write_t)(struct file *, const void *, size_t, loff_t *);
typedef int          (*fn_snprintf_t)(char *, size_t, const char *, ...);

static fn_filp_open_t    kfn_filp_open    = 0;
static fn_filp_close_t   kfn_filp_close   = 0;
static fn_kernel_read_t  kfn_kernel_read  = 0;
static fn_kernel_write_t kfn_kernel_write = 0;
static fn_snprintf_t     kfn_snprintf     = 0;

/* ═══════════════════════════════════════════════════════════
 * STATIC STORAGE
 * Saves raw volt_table text on load so we can restore on unload
 * without needing to know the original voltages at compile time.
 * 1024 bytes covers all 14 CPU OPPs on Exynos 9825 comfortably.
 * ═══════════════════════════════════════════════════════════ */
#define VTBL_BUF  1024

static char orig_cl0[VTBL_BUF];
static char orig_cl1[VTBL_BUF];
static char orig_gpu[VTBL_BUF];
static int  orig_cl0_len = 0;
static int  orig_cl1_len = 0;
static int  orig_gpu_len = 0;

/* Single shared work buffer — write path uses this */
static char work_buf[VTBL_BUF];

/* Which GPU path succeeded */
static const char *gpu_path_used = 0;

/* Applied flags */
static int uv_cl0_ok = 0;
static int uv_cl1_ok = 0;
static int uv_gpu_ok = 0;

/* ═══════════════════════════════════════════════════════════
 * HELPERS
 * ═══════════════════════════════════════════════════════════ */

static int is_err_ptr(void *p)
{
    return (unsigned long)p >= (unsigned long)-4096UL;
}

/* Simple string-to-unsigned-long; advances *sp past the number */
static unsigned long s2ul(const char *s, const char **sp)
{
    unsigned long v = 0;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    if (sp) *sp = s;
    return v;
}

/* Read sysfs file into dst (must be a STATIC buffer, not stack).
 * Returns byte count or 0 on failure. */
static int sysfs_read(const char *path, char *dst, int dstsize)
{
    struct file *f;
    loff_t pos = 0;
    ssize_t n;

    if (!kfn_filp_open || !kfn_kernel_read || !kfn_filp_close)
        return 0;
    f = kfn_filp_open(path, 0 /* O_RDONLY */, 0);
    if (!f || is_err_ptr(f)) {
        pr_warn("[EK-UV] open failed: %s\n", path);
        return 0;
    }
    n = kfn_kernel_read(f, dst, dstsize - 1, &pos);
    kfn_filp_close(f, 0);
    if (n <= 0) {
        pr_warn("[EK-UV] read failed: %s (n=%ld)\n", path, (long)n);
        return 0;
    }
    dst[n] = '\0';
    return (int)n;
}

/* Write buf to sysfs file. Returns 1 on success. */
static int sysfs_write(const char *path, const char *buf, int len)
{
    struct file *f;
    loff_t pos = 0;
    ssize_t n;

    if (!kfn_filp_open || !kfn_kernel_write || !kfn_filp_close)
        return 0;
    f = kfn_filp_open(path, 1 /* O_WRONLY */, 0);
    if (!f || is_err_ptr(f)) {
        pr_warn("[EK-UV] write-open failed: %s\n", path);
        return 0;
    }
    n = kfn_kernel_write(f, buf, len, &pos);
    kfn_filp_close(f, 0);
    if (n <= 0) {
        pr_warn("[EK-UV] write failed: %s (n=%ld)\n", path, (long)n);
        return 0;
    }
    return 1;
}

/* ═══════════════════════════════════════════════════════════
 * APPLY UNDERVOLT to one domain
 *
 * 1. Reads current volt_table text into orig_buf (static)
 * 2. Parses "freq_kHz volt_uV\n" pairs
 * 3. Subtracts offset_uv from each voltage (clamped to UV_FLOOR_UV)
 * 4. Writes modified table back via work_buf (static)
 *
 * Returns 1 on success.
 * ═══════════════════════════════════════════════════════════ */
static int do_undervolt(const char *path, unsigned long offset_uv,
                        char *orig_buf, int *orig_len, const char *label)
{
    const char *p;
    int out = 0;
    int count = 0;
    int n;

    n = sysfs_read(path, orig_buf, VTBL_BUF);
    if (!n) return 0;
    *orig_len = n;

    p = orig_buf;
    out = 0;
    while (*p && count < 32) {
        unsigned long freq, volt, nv;
        int wlen;

        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        if (!*p) break;
        freq = s2ul(p, &p);
        while (*p == ' ' || *p == '\t') p++;
        volt = s2ul(p, &p);
        if (!freq && !volt) break;

        nv = (volt > offset_uv + UV_FLOOR_UV) ? volt - offset_uv : UV_FLOOR_UV;

        /* Log first 3 OPPs so dmesg shows the applied change */
        if (count < 3)
            pr_warn("[EK-UV] %s OPP[%d]: %lu kHz  %lu->%lu uV\n",
                    label, count, freq, volt, nv);

        if (!kfn_snprintf) return 0;
        wlen = kfn_snprintf(work_buf + out, VTBL_BUF - out,
                            "%lu %lu\n", freq, nv);
        if (wlen > 0) out += wlen;
        count++;
    }

    if (!out || !count) {
        pr_warn("[EK-UV] %s: parse failed (0 OPPs found)\n", label);
        return 0;
    }

    pr_warn("[EK-UV] %s: %d OPPs, writing -%lu mV offset...\n",
            label, count, offset_uv / 1000);
    if (!sysfs_write(path, work_buf, out)) return 0;
    pr_warn("[EK-UV] %s: -%lu mV applied OK\n", label, offset_uv / 1000);
    return 1;
}

/* ═══════════════════════════════════════════════════════════
 * INIT
 * ═══════════════════════════════════════════════════════════ */
static long uv_init(const char *args, const char *event, void *__user reserved)
{
    pr_warn("[EK-UV] undervolt v1.1 loading...\n");
    pr_warn("[EK-UV] CL0=-%lu mV  CL1=-%lu mV  GPU=-%lu mV  floor=%lu mV\n",
            UV_OFFSET_CL0_UV / 1000, UV_OFFSET_CL1_UV / 1000,
            UV_OFFSET_GPU_UV / 1000, UV_FLOOR_UV / 1000);

    /* Resolve kernel file-I/O functions (same as extreme-tweaker) */
    kfn_filp_open    = (fn_filp_open_t)   kallsyms_lookup_name("filp_open");
    kfn_filp_close   = (fn_filp_close_t)  kallsyms_lookup_name("filp_close");
    kfn_kernel_read  = (fn_kernel_read_t) kallsyms_lookup_name("kernel_read");
    kfn_kernel_write = (fn_kernel_write_t)kallsyms_lookup_name("kernel_write");
    kfn_snprintf     = (fn_snprintf_t)    kallsyms_lookup_name("snprintf");

    pr_warn("[EK-UV] filp_open=%s filp_close=%s kernel_read=%s kernel_write=%s snprintf=%s\n",
            kfn_filp_open    ? "OK" : "NO",
            kfn_filp_close   ? "OK" : "NO",
            kfn_kernel_read  ? "OK" : "NO",
            kfn_kernel_write ? "OK" : "NO",
            kfn_snprintf     ? "OK" : "NO");

    if (!kfn_filp_open || !kfn_filp_close ||
        !kfn_kernel_read || !kfn_kernel_write || !kfn_snprintf) {
        pr_warn("[EK-UV] FATAL: kernel file-I/O symbols missing. Undervolt NOT applied.\n");
        pr_warn("[EK-UV] Check: su -c \"grep -E 'filp_open|kernel_write' /proc/kallsyms\"\n");
        return 0;
    }

    /* CPU clusters */
    uv_cl0_ok = do_undervolt(PATH_CL0, UV_OFFSET_CL0_UV,
                             orig_cl0, &orig_cl0_len, "CL0-little");
    uv_cl1_ok = do_undervolt(PATH_CL1, UV_OFFSET_CL1_UV,
                             orig_cl1, &orig_cl1_len, "CL1-big");

    /* GPU — try primary path, fall back to secondary */
    uv_gpu_ok = do_undervolt(PATH_GPU_A, UV_OFFSET_GPU_UV,
                             orig_gpu, &orig_gpu_len, "GPU");
    if (uv_gpu_ok) {
        gpu_path_used = PATH_GPU_A;
    } else {
        pr_warn("[EK-UV] GPU path A failed, trying path B...\n");
        uv_gpu_ok = do_undervolt(PATH_GPU_B, UV_OFFSET_GPU_UV,
                                 orig_gpu, &orig_gpu_len, "GPU");
        if (uv_gpu_ok)
            gpu_path_used = PATH_GPU_B;
    }

    pr_warn("[EK-UV] LOADED v1.1: CL0=%s CL1=%s GPU=%s\n",
            uv_cl0_ok ? "OK" : "SKIP",
            uv_cl1_ok ? "OK" : "SKIP",
            uv_gpu_ok ? "OK" : "SKIP");
    pr_warn("[EK-UV] Unload to restore ALL original voltages automatically.\n");
    return 0;
}

/* ═══════════════════════════════════════════════════════════
 * CTL0 — status shown in KSU-Next UI
 * ═══════════════════════════════════════════════════════════ */
static long uv_ctl0(const char *args, char *__user out_msg, int outlen)
{
    const char *msg =
        "undervolt v1.1 active\n"
        "CL0 little : -50 mV (50000 uV)\n"
        "CL1 big    : -50 mV (50000 uV)\n"
        "GPU Mali   : -25 mV (25000 uV)\n"
        "Floor      :  550 mV\n"
        "\nEdit UV_OFFSET_* in undervolt.c, rebuild, reinstall.\n"
        "\nVerify:\n"
        "  su -c \"dmesg | grep EK-UV\"\n"
        "  su -c \"cat /sys/devices/system/cpu/cpufreq/mp-cpufreq/cluster0_volt_table\"\n"
        "\nIf reboots occur: unload module or reduce offsets.\n";
    int len = 0;
    while (msg[len]) len++;
    compat_copy_to_user(out_msg, msg, len + 1);
    return 0;
}

/* ═══════════════════════════════════════════════════════════
 * EXIT — restore original volt_tables verbatim
 * ═══════════════════════════════════════════════════════════ */
static long uv_exit(void *__user reserved)
{
    pr_warn("[EK-UV] unloading — restoring original voltages...\n");

    if (uv_cl0_ok && orig_cl0_len > 0) {
        if (sysfs_write(PATH_CL0, orig_cl0, orig_cl0_len))
            pr_warn("[EK-UV] CL0-little: restored OK\n");
        else
            pr_warn("[EK-UV] CL0-little: restore write failed\n");
    }
    if (uv_cl1_ok && orig_cl1_len > 0) {
        if (sysfs_write(PATH_CL1, orig_cl1, orig_cl1_len))
            pr_warn("[EK-UV] CL1-big: restored OK\n");
        else
            pr_warn("[EK-UV] CL1-big: restore write failed\n");
    }
    if (uv_gpu_ok && orig_gpu_len > 0 && gpu_path_used) {
        if (sysfs_write(gpu_path_used, orig_gpu, orig_gpu_len))
            pr_warn("[EK-UV] GPU: restored OK\n");
        else
            pr_warn("[EK-UV] GPU: restore write failed\n");
    }

    pr_warn("[EK-UV] UNLOADED. Chip back to stock Samsung voltages.\n");
    return 0;
}

KPM_INIT(uv_init);
KPM_CTL0(uv_ctl0);
KPM_EXIT(uv_exit);
