/* SPDX-License-Identifier: GPL-2.0 */
/*
 * undervolt.kpm  v1.0  -  ExtremeKernel CPU/GPU Undervolt
 * Samsung Galaxy S20 Ultra (d2s, Exynos 9825), kernel 4.14
 *
 * ============================================================
 * WHAT IS UNDERVOLTING?
 * ============================================================
 * Every CPU frequency step (DVFS OPP) has a voltage assigned
 * by Samsung — always set conservatively HIGH to guarantee the
 * chip works even on the worst silicon sample from the factory.
 * Most chips can run at the SAME frequencies with LOWER voltage.
 *
 * Reducing voltage by even 25-50 mV per step delivers:
 *   - Lower heat (chip runs cooler)
 *   - Better battery life (less power consumed)
 *   - No loss in performance (same clock speeds)
 *
 * If the undervolts are too aggressive the chip becomes
 * unstable (random reboots, kernel panics). Start small.
 *
 * ============================================================
 * HOW THIS KPM WORKS (vs. hKtweaks Android app)
 * ============================================================
 * hKtweaks writes to sysfs from an Android app in userspace.
 * This KPM does the same writes from kernel space:
 *   - No Android app needed
 *   - Loads at boot via KSU-Next before userspace
 *   - Auto-restores EVERY original voltage on unload (trash icon)
 *   - Works even with SELinux enforcing
 *
 * ============================================================
 * CONFIGURATION — edit the values below and rebuild
 * ============================================================
 *   UV_OFFSET_CL0_UV  : little CPU cluster offset (µV, positive = reduction)
 *   UV_OFFSET_CL1_UV  : big   CPU cluster offset
 *   UV_OFFSET_GPU_UV  : GPU offset
 *   UV_FLOOR_UV       : never go below this voltage (safety floor)
 *
 * Safe starting point: -50 mV = 50000 µV on all clusters.
 * If stable for 24h, try -75 mV. If reboots occur, reduce.
 *
 * ============================================================
 * VERIFY
 * ============================================================
 *   su -c "dmesg | grep EK-UV"
 *   su -c "cat /sys/devices/system/cpu/cpufreq/mp-cpufreq/cluster0_volt_table"
 *   su -c "cat /sys/devices/system/cpu/cpufreq/mp-cpufreq/cluster1_volt_table"
 */

#include <compiler.h>
#include <kpmodule.h>
#include <kputils.h>
#include <linux/printk.h>
#include <linux/string.h>
#include <kallsyms.h>

KPM_NAME("undervolt");
KPM_VERSION("1.0.0");
KPM_LICENSE("GPL v2");
KPM_AUTHOR("ExtremeKernel");
KPM_DESCRIPTION("CPU/GPU undervolt for d2s Exynos 9825. Edit offsets, rebuild, reinstall. Full restore on unload.");

/* ============================================================
 * USER-CONFIGURABLE OFFSETS (µV, positive = reduce voltage)
 * Start conservative. Rebuild after editing.
 * ============================================================ */
#define UV_OFFSET_CL0_UV   50000UL   /* little cluster: -50 mV */
#define UV_OFFSET_CL1_UV   50000UL   /* big   cluster: -50 mV */
#define UV_OFFSET_GPU_UV   25000UL   /* GPU:           -25 mV  */
#define UV_FLOOR_UV       550000UL   /* absolute minimum: 0.55 V */
#define MAX_OPP            32        /* max freq steps per domain */

/* ============================================================
 * VOLTAGE TABLE PATHS
 * ============================================================ */
#define PATH_CL0  "/sys/devices/system/cpu/cpufreq/mp-cpufreq/cluster0_volt_table"
#define PATH_CL1  "/sys/devices/system/cpu/cpufreq/mp-cpufreq/cluster1_volt_table"
/* GPU: Exynos 9825 Mali-G76. Try both known paths. */
#define PATH_GPU_A  "/sys/devices/platform/11500000.mali/volt_table"
#define PATH_GPU_B  "/sys/kernel/gpu/gpu_volt_table"

/* ============================================================
 * KERNEL FUNCTION TYPES (resolved via kallsyms at runtime)
 * ============================================================ */
typedef struct file *(*fn_filp_open_t)(const char *, int, umode_t);
typedef int          (*fn_filp_close_t)(struct file *, void *);
typedef ssize_t      (*fn_kernel_read_t)(struct file *, void *, size_t, loff_t *);
typedef ssize_t      (*fn_kernel_write_t)(struct file *, const void *, size_t, loff_t *);
typedef int          (*fn_snprintf_t)(char *, size_t, const char *, ...);
typedef long         (*fn_strtol_t)(const char *, char **, int);

static fn_filp_open_t   kfn_filp_open   = 0;
static fn_filp_close_t  kfn_filp_close  = 0;
static fn_kernel_read_t kfn_kernel_read = 0;
static fn_kernel_write_t kfn_kernel_write = 0;
static fn_snprintf_t    kfn_snprintf    = 0;

/* ============================================================
 * SAVED ORIGINAL VOLT TABLES FOR RESTORE
 * Format: freq_kHz voltage_uV pairs
 * ============================================================ */
typedef struct {
    unsigned long freq[MAX_OPP];
    unsigned long volt[MAX_OPP];
    int           count;
    char          path[128];
    int           valid;
} volt_table_t;

static volt_table_t orig_cl0 = { .valid = 0 };
static volt_table_t orig_cl1 = { .valid = 0 };
static volt_table_t orig_gpu = { .valid = 0 };

/* ============================================================
 * HELPER: simple string-to-unsigned-long (no stdlib needed)
 * ============================================================ */
static unsigned long parse_ulong(const char *s, const char **end_out)
{
    unsigned long val = 0;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (*s - '0');
        s++;
    }
    if (end_out) *end_out = s;
    return val;
}

/* ============================================================
 * HELPER: check if a struct file * is an error pointer
 * IS_ERR equivalent: kernel encodes errors as large negative ptrs
 * ============================================================ */
static int is_err_ptr(void *ptr)
{
    return (unsigned long)ptr >= (unsigned long)-4096UL;
}

/* ============================================================
 * Read and parse a volt_table sysfs file.
 * Returns 1 on success, 0 on failure.
 * ============================================================ */
static int read_volt_table(const char *path, volt_table_t *out)
{
    struct file *f;
    char buf[2048];
    ssize_t n;
    loff_t pos = 0;
    const char *p;
    int i = 0;

    if (!kfn_filp_open || !kfn_kernel_read || !kfn_filp_close) return 0;

    f = kfn_filp_open(path, 0 /* O_RDONLY */, 0);
    if (!f || is_err_ptr(f)) {
        pr_warn("[EK-UV] open failed: %s\n", path);
        return 0;
    }

    n = kfn_kernel_read(f, buf, sizeof(buf) - 1, &pos);
    kfn_filp_close(f, 0);

    if (n <= 0) {
        pr_warn("[EK-UV] read failed: %s (n=%ld)\n", path, (long)n);
        return 0;
    }
    buf[n] = '\0';

    /* Parse "freq_kHz voltage_uV\n" lines */
    p = buf;
    while (*p && i < MAX_OPP) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        if (!*p) break;
        out->freq[i] = parse_ulong(p, &p);
        if (!*p) break;
        while (*p == ' ' || *p == '\t') p++;
        out->volt[i] = parse_ulong(p, &p);
        if (out->freq[i] == 0 && out->volt[i] == 0) break;
        i++;
    }

    out->count = i;
    /* copy path safely */
    {
        int l = 0;
        while (path[l] && l < 127) { out->path[l] = path[l]; l++; }
        out->path[l] = '\0';
    }
    out->valid = (i > 0);
    return out->valid;
}

/* ============================================================
 * Write a volt_table back to sysfs.
 * Format: "freq_kHz voltage_uV\n" per line.
 * ============================================================ */
static int write_volt_table(volt_table_t *tbl)
{
    struct file *f;
    char buf[2048];
    char linebuf[64];
    int pos_buf = 0;
    int i;
    loff_t pos = 0;
    ssize_t n;

    if (!kfn_filp_open || !kfn_kernel_write || !kfn_filp_close || !kfn_snprintf) return 0;
    if (!tbl->valid || tbl->count == 0) return 0;

    /* Build the write buffer */
    buf[0] = '\0';
    for (i = 0; i < tbl->count && pos_buf < (int)(sizeof(buf) - 64); i++) {
        int len = kfn_snprintf(linebuf, sizeof(linebuf), "%lu %lu\n",
                               tbl->freq[i], tbl->volt[i]);
        if (len > 0 && pos_buf + len < (int)sizeof(buf)) {
            int j;
            for (j = 0; j < len; j++) buf[pos_buf + j] = linebuf[j];
            pos_buf += len;
        }
    }
    buf[pos_buf] = '\0';

    f = kfn_filp_open(tbl->path, 1 /* O_WRONLY */, 0);
    if (!f || is_err_ptr(f)) {
        pr_warn("[EK-UV] write open failed: %s\n", tbl->path);
        return 0;
    }

    n = kfn_kernel_write(f, buf, pos_buf, &pos);
    kfn_filp_close(f, 0);

    if (n <= 0) {
        pr_warn("[EK-UV] write failed: %s (n=%ld)\n", tbl->path, (long)n);
        return 0;
    }
    return 1;
}

/* ============================================================
 * Apply undervolt offset to a volt_table and write it back.
 * Saves originals into *orig before modifying.
 * ============================================================ */
static void apply_undervolt(volt_table_t *orig, const char *path,
                             unsigned long offset_uv, const char *label)
{
    volt_table_t modified;
    int i;

    if (!read_volt_table(path, orig)) {
        pr_warn("[EK-UV] %s: cannot read volt table at %s\n", label, path);
        return;
    }

    /* Copy to working table */
    for (i = 0; i < orig->count; i++) {
        modified.freq[i] = orig->freq[i];
        modified.volt[i] = orig->volt[i];
        /* Apply offset, clamp to floor */
        if (modified.volt[i] > offset_uv + UV_FLOOR_UV)
            modified.volt[i] -= offset_uv;
        else
            modified.volt[i] = UV_FLOOR_UV;
    }
    modified.count = orig->count;
    modified.valid = 1;
    {
        int l = 0;
        while (path[l] && l < 127) { modified.path[l] = path[l]; l++; }
        modified.path[l] = '\0';
    }

    pr_warn("[EK-UV] %s: %d OPPs found, applying -%lu mV offset...\n",
            label, orig->count, offset_uv / 1000);

    /* Log first 3 and last step for quick visual check */
    for (i = 0; i < orig->count; i++) {
        if (i < 3 || i == orig->count - 1) {
            pr_warn("[EK-UV]   %s OPP[%d]: %lu kHz  %lu -> %lu uV\n",
                    label, i, orig->freq[i], orig->volt[i], modified.volt[i]);
        }
    }

    if (write_volt_table(&modified))
        pr_warn("[EK-UV] %s: undervolt applied OK (-%lu mV)\n", label, offset_uv / 1000);
    else
        pr_warn("[EK-UV] %s: write failed — sysfs path may differ on this build\n", label);
}

/* ============================================================
 * INIT
 * ============================================================ */
static long uv_init(const char *args, const char *event, void *__user reserved)
{
    pr_warn("[EK-UV] undervolt v1.0 loading...\n");
    pr_warn("[EK-UV] offsets: CL0=-%lu mV, CL1=-%lu mV, GPU=-%lu mV, floor=%lu mV\n",
            UV_OFFSET_CL0_UV / 1000, UV_OFFSET_CL1_UV / 1000,
            UV_OFFSET_GPU_UV / 1000, UV_FLOOR_UV / 1000);

    /* Resolve kernel functions */
    kfn_filp_open    = (fn_filp_open_t)   kallsyms_lookup_name("filp_open");
    kfn_filp_close   = (fn_filp_close_t)  kallsyms_lookup_name("filp_close");
    kfn_kernel_read  = (fn_kernel_read_t) kallsyms_lookup_name("kernel_read");
    kfn_kernel_write = (fn_kernel_write_t)kallsyms_lookup_name("kernel_write");
    kfn_snprintf     = (fn_snprintf_t)    kallsyms_lookup_name("snprintf");

    pr_warn("[EK-UV] filp_open   : %s\n", kfn_filp_open    ? "OK" : "MISSING");
    pr_warn("[EK-UV] filp_close  : %s\n", kfn_filp_close   ? "OK" : "MISSING");
    pr_warn("[EK-UV] kernel_read : %s\n", kfn_kernel_read  ? "OK" : "MISSING");
    pr_warn("[EK-UV] kernel_write: %s\n", kfn_kernel_write ? "OK" : "MISSING");

    if (!kfn_filp_open || !kfn_filp_close || !kfn_kernel_read || !kfn_kernel_write) {
        pr_warn("[EK-UV] FATAL: required kernel functions not found — undervolt NOT applied.\n");
        pr_warn("[EK-UV] Try: su -c \"cat /proc/kallsyms | grep -E 'filp_open|kernel_write'\"\n");
        return 0;
    }

    /* Apply undervolts */
    apply_undervolt(&orig_cl0, PATH_CL0, UV_OFFSET_CL0_UV, "CL0-little");
    apply_undervolt(&orig_cl1, PATH_CL1, UV_OFFSET_CL1_UV, "CL1-big");

    /* Try GPU path A, fall back to path B */
    if (!read_volt_table(PATH_GPU_A, &orig_gpu)) {
        apply_undervolt(&orig_gpu, PATH_GPU_B, UV_OFFSET_GPU_UV, "GPU");
    } else {
        /* Reset and do proper apply */
        orig_gpu.valid = 0;
        apply_undervolt(&orig_gpu, PATH_GPU_A, UV_OFFSET_GPU_UV, "GPU");
    }

    pr_warn("[EK-UV] LOADED v1.0. Unload to restore ALL original voltages.\n");
    pr_warn("[EK-UV] Verify: su -c \"cat " PATH_CL0 "\"\n");
    pr_warn("[EK-UV] Verify: su -c \"cat " PATH_CL1 "\"\n");
    return 0;
}

/* ============================================================
 * CTL0 — status / help text
 * ============================================================ */
static long uv_ctl0(const char *args, char *__user out_msg, int outlen)
{
    const char *msg =
        "undervolt v1.0 active\n"
        "CL0 (little) : -" __stringify(UV_OFFSET_CL0_UV) " uV\n"
        "CL1 (big)    : -" __stringify(UV_OFFSET_CL1_UV) " uV\n"
        "GPU (Mali)   : -" __stringify(UV_OFFSET_GPU_UV) " uV\n"
        "Floor        :  " __stringify(UV_FLOOR_UV) " uV\n"
        "\nTo change offsets: edit kpms/undervolt/undervolt.c,\n"
        "run build-kpm.yml, reinstall the .kpm file.\n"
        "\nVerify:\n"
        "  su -c \"dmesg | grep EK-UV\"\n"
        "  su -c \"cat /sys/devices/system/cpu/cpufreq/mp-cpufreq/cluster0_volt_table\"\n"
        "  su -c \"cat /sys/devices/system/cpu/cpufreq/mp-cpufreq/cluster1_volt_table\"\n"
        "\nIf unstable (reboots): unload module or reduce offsets and rebuild.\n";
    int len = 0;
    while (msg[len]) len++;
    compat_copy_to_user(out_msg, msg, len + 1);
    return 0;
}

/* ============================================================
 * EXIT — restore ALL original voltages
 * ============================================================ */
static long uv_exit(void *__user reserved)
{
    pr_warn("[EK-UV] unloading — restoring ALL original voltages...\n");

    if (orig_cl0.valid) {
        if (write_volt_table(&orig_cl0))
            pr_warn("[EK-UV] CL0-little: original voltages restored\n");
        else
            pr_warn("[EK-UV] CL0-little: restore write failed\n");
    }
    if (orig_cl1.valid) {
        if (write_volt_table(&orig_cl1))
            pr_warn("[EK-UV] CL1-big: original voltages restored\n");
        else
            pr_warn("[EK-UV] CL1-big: restore write failed\n");
    }
    if (orig_gpu.valid) {
        if (write_volt_table(&orig_gpu))
            pr_warn("[EK-UV] GPU: original voltages restored\n");
        else
            pr_warn("[EK-UV] GPU: restore write failed\n");
    }

    pr_warn("[EK-UV] UNLOADED. Chip is back to Samsung stock voltages.\n");
    return 0;
}

KPM_INIT(uv_init);
KPM_CTL0(uv_ctl0);
KPM_EXIT(uv_exit);
