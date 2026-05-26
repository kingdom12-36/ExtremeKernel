#!/system/bin/sh
# ExtremeKernel Undervolt Stability Test v1.0
# Run as root on the device after flashing undervolt.kpm
# Usage: su -c "sh /data/local/tmp/test-stability.sh"
# ======================================================

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
PASS=0; FAIL=0

LOG_FILE="/data/local/tmp/ek-uvtest-$(date +%Y%m%d-%H%M%S).log"
exec > >(tee -a "$LOG_FILE") 2>&1

echo "========================================"
echo "  ExtremeKernel Undervolt Stability Test"
echo "  $(date)"
echo "========================================"
echo ""

# ── 1. Check KPM is loaded ──────────────────────────────
echo "[1/5] Checking undervolt KPM..."
if dmesg | grep -q "EK-UV"; then
    echo "${GREEN}[PASS]${NC} undervolt.kpm loaded:"
    dmesg | grep "EK-UV" | tail -5 | sed 's/^/       /'
    PASS=$((PASS+1))
else
    echo "${RED}[FAIL]${NC} EK-UV not found in dmesg. Is the KPM loaded?"
    echo "       Try: kpatch load /data/adb/kpatch/undervolt.kpm"
    FAIL=$((FAIL+1))
fi
echo ""

# ── 2. Read baseline volt tables ────────────────────────
echo "[2/5] Reading voltage tables..."
for TABLE in cluster0_volt_table cluster1_volt_table; do
    PATH_T="/sys/devices/system/cpu/cpufreq/mp-cpufreq/$TABLE"
    if [ -f "$PATH_T" ]; then
        LOWEST=$(cat "$PATH_T" | awk '{print $2}' | sort -n | head -1)
        echo "  $TABLE  →  lowest entry: ${LOWEST} uV"
    else
        echo "${YELLOW}[WARN]${NC} $PATH_T not found (normal if CPU is idle-collapsed)"
    fi
done
GPU_V="/sys/devices/platform/11500000.mali/volt_table"
if [ -f "$GPU_V" ]; then
    GPU_LOW=$(cat "$GPU_V" | awk '{print $2}' | sort -n | head -1)
    echo "  gpu volt_table        →  lowest entry: ${GPU_LOW} uV"
else
    echo "${YELLOW}[WARN]${NC} GPU volt_table not found"
fi
echo ""

# ── 3. Read baseline temps ──────────────────────────────
echo "[3/5] Baseline temperatures:"
TZONES=$(ls /sys/class/thermal/thermal_zone*/temp 2>/dev/null | head -8)
for f in $TZONES; do
    ZONE=$(echo "$f" | grep -o 'thermal_zone[0-9]*')
    TEMP=$(cat "$f" 2>/dev/null)
    TYPE=$(cat "/sys/class/thermal/${ZONE}/type" 2>/dev/null || echo "unknown")
    TEMP_C=$(awk "BEGIN{printf \"%.1f\", $TEMP/1000}")
    echo "  $TYPE ($ZONE): ${TEMP_C}°C"
done
echo ""

# ── 4. CPU stress test (60s, all cores) ─────────────────
echo "[4/5] Stress test — 60s all-core load..."
echo "       (watching for thermal shutdown, kernel panics, lockups)"
echo ""

DMESG_BEFORE=$(dmesg | wc -l)
NCPUS=$(grep -c ^processor /proc/cpuinfo 2>/dev/null || echo 8)
CRASH=0

# Spawn one stress worker per CPU core
# Workers do integer arithmetic (safe, no FPU, no memory pressure)
stress_worker() {
    local N=0
    local END=$(($(date +%s) + 60))
    while [ $(date +%s) -lt $END ]; do
        N=$(( (N * 6364136223846793005 + 1442695040888963407) & 0x7FFFFFFFFFFFFFFF ))
    done
}

echo "  Spawning $NCPUS worker threads..."
for i in $(seq 1 $NCPUS); do
    (stress_worker) &
done

# Monitor for 60s, check temps every 5s
MAX_TEMP=0
for TICK in $(seq 1 12); do
    sleep 5
    ELAPSED=$((TICK * 5))

    # Read hottest thermal zone
    HOT=0
    for f in $TZONES; do
        T=$(cat "$f" 2>/dev/null || echo 0)
        [ "$T" -gt "$HOT" ] && HOT=$T
    done
    HOT_C=$(awk "BEGIN{printf \"%.1f\", $HOT/1000}")
    [ "$HOT" -gt "$MAX_TEMP" ] && MAX_TEMP=$HOT

    # Check for new kernel errors since test started
    NEW_ERRS=$(dmesg | tail -n +$DMESG_BEFORE | grep -Ec "BUG:|kernel panic|Oops:|Unable to handle|EK-UV.*ERR" || true)

    WARN=""
    [ "$HOT" -gt 85000 ] && WARN="${RED}THERMAL WARNING${NC}" && CRASH=1
    [ "$NEW_ERRS" -gt 0 ] && WARN="${RED}KERNEL ERRORS: $NEW_ERRS${NC}" && CRASH=1

    printf "  [%2ds] peak temp: %5s°C  new kernel errors: %d  %s\n" \
        "$ELAPSED" "$HOT_C" "$NEW_ERRS" "$WARN"

    # Abort early if it's getting dangerous
    [ "$CRASH" -eq 1 ] && echo "  Aborting early — see warnings above." && break
done

# Wait for workers to finish
wait

MAX_C=$(awk "BEGIN{printf \"%.1f\", $MAX_TEMP/1000}")
echo ""
echo "  Stress complete. Peak temperature: ${MAX_C}°C"

if [ "$CRASH" -eq 0 ]; then
    echo "${GREEN}[PASS]${NC} Completed 60s stress test without thermal or kernel errors."
    PASS=$((PASS+1))
else
    echo "${RED}[FAIL]${NC} Issues detected during stress test. Check log for details."
    FAIL=$((FAIL+1))
fi
echo ""

# ── 5. Post-stress dmesg scan ───────────────────────────
echo "[5/5] Scanning dmesg for undervolt-related errors..."
NEW_ERRS_DETAIL=$(dmesg | tail -n +$DMESG_BEFORE | grep -E "BUG:|panic|Oops:|EK-UV.*ERR|cpu.*hang" | head -10)
if [ -z "$NEW_ERRS_DETAIL" ]; then
    echo "${GREEN}[PASS]${NC} No crash/panic/BUG messages found."
    PASS=$((PASS+1))
else
    echo "${RED}[FAIL]${NC} Kernel messages of concern:"
    echo "$NEW_ERRS_DETAIL" | sed 's/^/         /'
    FAIL=$((FAIL+1))
fi
echo ""

# ── Summary ─────────────────────────────────────────────
echo "========================================"
echo "  RESULT: $PASS passed, $FAIL failed"
echo "  Log saved to: $LOG_FILE"
if [ "$FAIL" -eq 0 ]; then
    echo "  STATUS: ${GREEN}STABLE${NC} — undervolt is safe at current offsets"
    echo ""
    echo "  If you want to go deeper:"
    echo "   - Edit UV_OFFSET_CL0_UV / UV_OFFSET_CL1_UV in undervolt.c"
    echo "   - Increase by 12500 uV (12.5mV) steps, rebuild, retest"
    echo "   - Stop if this test fails or the device reboots mid-stress"
else
    echo "  STATUS: ${RED}UNSTABLE${NC} — reduce offsets or check voltage floor"
fi
echo "========================================"
