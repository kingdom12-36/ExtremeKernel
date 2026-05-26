# undervolt.kpm

**ExtremeKernel CPU/GPU Undervolt — Exynos 9825 (d2s), Samsung 4.14**

---

## What is undervolting?

Samsung ships every phone with conservative (high) voltages in the DVFS table
to guarantee the chip works even on the worst silicon sample from the factory.
Your specific chip very likely runs stable at lower voltages.

Reducing voltage per frequency step gives you:
- **Less heat** — chip runs cooler under load
- **Better battery life** — less power consumed at every frequency
- **Same performance** — clock speeds are unchanged

If you go too aggressive the chip becomes unstable (random reboots). Start
small and work up.

---

## Why this is better than hKtweaks / sysfs scripts

| | hKtweaks app | undervolt.kpm |
|---|---|---|
| Requires Android app | ✅ yes | ❌ no |
| Loads at boot | ❌ after userspace | ✅ kernel boot |
| SELinux issues | possible | ❌ none |
| Auto-restores on unload | ❌ no | ✅ always |
| Modifies files on disk | ❌ no | ❌ no |

---

## Configuration — edit `undervolt.c`

```c
#define UV_OFFSET_CL0_UV   50000UL   /* little CPU cluster: -50 mV */
#define UV_OFFSET_CL1_UV   50000UL   /* big   CPU cluster:  -50 mV */
#define UV_OFFSET_GPU_UV   25000UL   /* GPU (Mali G76):     -25 mV */
#define UV_FLOOR_UV       550000UL   /* safety floor:      0.55 V  */
```

**Recommended progression:**

| Round | CL0 | CL1 | GPU | Test |
|-------|-----|-----|-----|------|
| 1 | -50 mV | -50 mV | -25 mV | 24h normal use |
| 2 | -75 mV | -75 mV | -50 mV | 24h + gaming |
| 3 | -100 mV | -100 mV | -75 mV | stress test |

If you get random reboots at any step, go back one level.

---

## How to apply

1. Edit the `#define` values in `undervolt.c`
2. Trigger the `build-kpm.yml` GitHub Action
3. Download `undervolt.kpm` from the release
4. Install via **KSU-Next → KPModule** tab
5. Verify (see below)

---

## Verify it worked

```bash
# Check dmesg for per-OPP before/after log
su -c "dmesg | grep EK-UV"

# Check the live volt tables directly
su -c "cat /sys/devices/system/cpu/cpufreq/mp-cpufreq/cluster0_volt_table"
su -c "cat /sys/devices/system/cpu/cpufreq/mp-cpufreq/cluster1_volt_table"
su -c "cat /sys/devices/platform/11500000.mali/volt_table"
```

The dmesg output shows the original and new voltage for each OPP.

---

## Restore / unload

Tap the **trash icon** in KSU-Next KPModule tab.
The module writes ALL original voltages back before unloading.
Zero permanent changes.

---

## Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| `write failed` in dmesg | sysfs path doesn't exist | Run `su -c "ls /sys/devices/system/cpu/cpufreq/mp-cpufreq/"` and update paths in code |
| `MISSING` for filp_open | Samsung stripped symbol | Very rare on 4.14 — open an issue |
| Random reboots | Offset too aggressive | Reduce by 25 mV per round |
| GPU path fails | Mali address differs | Try PATH_GPU_B in code, check `su -c "ls /sys/kernel/gpu/"` |

---

## Folder structure

```
kpms/undervolt/
  undervolt.c    ← edit offsets here, rebuild to apply
  README.md      ← you are here
```
