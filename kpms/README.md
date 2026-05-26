# ExtremeKernel KPMs

KPatch modules for the d2s (Exynos 9825) kernel. All are **temporary** —
loaded at runtime, full restore on unload. No permanent kernel changes.

---

## Modules

### battery-saver
Optimises VM and I/O scheduling for maximum battery life.

| Symbol | Stock (Samsung) | Applied |
|--------|----------------|---------|
| dirty_writeback_interval | 500 cs | 3000 cs (30s) |
| dirty_expire_interval | 200 cs | 9000 cs (90s) |
| laptop_mode | 0 | 5 |
| vm_swappiness | **160** | 20 |
| vfs_cache_pressure | 100 | 50 |
| sched_migration_cost_ns | 500000 | 5000000 |

### extreme-tweaker
Network and TCP optimisations, dirty page ratio tuning.

| Symbol | Applied |
|--------|---------|
| vm_dirty_bytes / vm_dirty_background_bytes | 0 (unlock ratio control) |
| vm_dirty_ratio | 30% |
| vm_dirty_background_ratio | 8% |
| sysctl_tcp_slow_start_after_idle | 0 |

### disabler
Disables kernel debug/panic features that cause noise or unnecessary reboots.
**Configurable** — see `kpms/disabler/tweaks.h`.

### undervolt ⚡ NEW
Reduces CPU and GPU voltages by a fixed offset across all DVFS frequency steps.
**No app required.** Same effect as hKtweaks voltage control, but from kernel space.
**Configurable** — edit offsets in `kpms/undervolt/undervolt.c`.

| Target | Default offset |
|--------|---------------|
| CPU little cluster (CL0) | **-50 mV** |
| CPU big cluster (CL1) | **-50 mV** |
| GPU (Mali G76) | **-25 mV** |
| Safety floor | 550 mV |

See `kpms/undervolt/README.md` for the full progression guide.

---

## How to add a tweak to `disabler`

1. Open `kpms/disabler/tweaks.h`
2. Find the symbol name on your device:
   ```bash
   su -c "cat /proc/kallsyms | grep -i your_keyword"
   ```
3. Add a line:
   ```c
   EK_DISABLE("symbol_name", 0)
   ```
4. For `unsigned long` symbols, add to the `long_tweaks[]` array in `disabler.c` instead.
5. Trigger the `build-kpm.yml` GitHub Action → download `.kpm` → reinstall.

---

## Verify any KPM is working

```bash
# battery-saver
su -c "dmesg | grep EK-BATT"
su -c "cat /proc/sys/vm/swappiness"          # expect: 20

# disabler
su -c "dmesg | grep EK-DISABLE"
su -c "cat /proc/sys/kernel/hung_task_timeout_secs"  # expect: 0

# extreme-tweaker
su -c "dmesg | grep EK-TWEAK"
su -c "cat /proc/sys/vm/dirty_ratio"         # expect: 30

# undervolt
su -c "dmesg | grep EK-UV"
su -c "cat /sys/devices/system/cpu/cpufreq/mp-cpufreq/cluster0_volt_table"
su -c "cat /sys/devices/system/cpu/cpufreq/mp-cpufreq/cluster1_volt_table"
```

---

## Full restore

Tap the trash icon in KSU-Next KPModule tab → module unloads and every
value reverts to what it was before load. Zero permanent changes.

---

## Folder structure

```
kpms/
  README.md                  ← you are here
  battery-saver/
    battery-saver.c          ← edit to change VM/IO tweaks
  extreme-tweaker/
    extreme-tweaker.c        ← edit to change network tweaks
  disabler/
    tweaks.h                 ← EDIT THIS to add/remove debug disablers
    disabler.c               ← main code (add unsigned long cases here)
  undervolt/
    undervolt.c              ← EDIT THIS to change mV offsets
    README.md                ← full guide: what undervolting is, how to tune
```
