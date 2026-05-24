#!/system/bin/sh
# ============================================================
# wifi_monitor_attacks.sh — WiFi Attack Detector
# ExtremeKernel | KPatch+Monitor Variant
# Auto-installed to: /data/adb/service.d/wifi_monitor_attacks.sh
# Runs at boot via KernelSU/Magisk
# Logs attacks to: /sdcard/logattacks.txt
# ============================================================

LOGFILE="/sdcard/logattacks.txt"
MON_IF="mon0"
WIFI_IF="wlan0"

log() {
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] $*" >> "$LOGFILE"
}

# Wait for system to fully boot before starting
sleep 30

log "============================================"
log "WiFi Attack Monitor Started"
log "Kernel: $(uname -r)"
log "Device: $(getprop ro.product.model 2>/dev/null || echo unknown)"
log "Android: $(getprop ro.build.version.release 2>/dev/null || echo unknown)"
log "============================================"

# ---- Method 1: Try to create monitor interface (needs WL_MONITOR in kernel) ----
create_monitor_iface() {
    iw dev "$WIFI_IF" interface add "$MON_IF" type monitor 2>/dev/null \
        && ip link set "$MON_IF" up 2>/dev/null \
        && log "[INIT] Monitor interface $MON_IF created (WL_MONITOR active)" \
        && return 0
    log "[INIT] Monitor interface unavailable — using passive detection methods"
    return 1
}

# ---- Method 2: tcpdump on monitor interface (802.11 mgmt frames) ----
# Detects deauth/disassoc/beacon-flood at the frame level
watch_tcpdump() {
    ip link show "$MON_IF" > /dev/null 2>&1 || return
    log "[TCPDUMP] Starting 802.11 capture on $MON_IF..."
    tcpdump -i "$MON_IF" -nn -e -l 2>/dev/null | while read -r LINE; do
        case "$LINE" in
            *"Deauthentication"*|*"deauth"*|*"DeAuth"*)
                log "[ATTACK][DEAUTH] $LINE" ;;
            *"Disassociation"*|*"disassoc"*|*"Disassoc"*)
                log "[ATTACK][DISASSOC] $LINE" ;;
            *"Authentication"*) log "[AUTH] $LINE" ;;
        esac
    done &
    log "[TCPDUMP] PID: $! — monitoring $MON_IF"
}

# ---- Method 3: dmesg watcher for BCM4375 DHD driver events ----
# Samsung's DHD driver prints deauth/disconnect events to the kernel log.
# Works even without monitor mode — always active.
watch_dmesg() {
    log "[DMESG] Starting BCM4375 DHD event watcher..."
    dmesg -w 2>/dev/null | while read -r LINE; do
        case "$LINE" in
            *"deauth"*|*"DEAUTH"*)
                log "[ATTACK][DMESG][DEAUTH] $LINE" ;;
            *"disassoc"*|*"DISASSOC"*)
                log "[ATTACK][DMESG][DISASSOC] $LINE" ;;
            *"beacon loss"*|*"BEACON_LOSS"*|*"beacon_loss"*)
                log "[ATTACK][DMESG][BEACON LOSS] Possible jamming/deauth — $LINE" ;;
            *"reason=6"*|*"reason=7"*)
                log "[ATTACK][DMESG][REASON CODE 6/7] Class 2/3 frame from unauth STA — $LINE" ;;
            *"reason=1"*)
                log "[SUSPECT][DMESG][REASON=1] Unspecified disconnect — $LINE" ;;
            *"disconnect"*|*"DISCONNECT"*)
                log "[INFO][DMESG][DISCONNECT] $LINE" ;;
        esac
    done &
    log "[DMESG] Watcher PID: $!"
}

# ---- Method 4: wpa_supplicant event monitor ----
# Catches disconnect/deauth events reported to wpa_supplicant.
# Useful to confirm attacks seen in dmesg with higher-level context.
watch_wpa() {
    # Try common wpa_supplicant socket locations on Samsung
    for SOCK_DIR in "/data/vendor/wifi/wpa/sockets" "/data/misc/wifi/sockets"; do
        if [ -d "$SOCK_DIR" ]; then
            log "[WPA] Monitoring wpa_supplicant at $SOCK_DIR..."
            wpa_cli -p "$SOCK_DIR" -i "$WIFI_IF" -a /dev/null 2>/dev/null | \
            while read -r EVENT; do
                case "$EVENT" in
                    *DISCONNECTED*)  log "[ATTACK][WPA][DISCONNECTED] $EVENT" ;;
                    *DEAUTH*)        log "[ATTACK][WPA][DEAUTH] $EVENT" ;;
                    *DISASSOC*)      log "[ATTACK][WPA][DISASSOC] $EVENT" ;;
                    *AUTH_FAILED*)   log "[SUSPECT][WPA][AUTH_FAILED] Possible brute-force — $EVENT" ;;
                    *TEMP_DISABLED*) log "[SUSPECT][WPA][TEMP_DISABLED] Network blocked — $EVENT" ;;
                esac
            done &
            log "[WPA] Watcher PID: $!"
            return
        fi
    done
    log "[WPA] wpa_supplicant socket not found — skipping wpa_cli method"
}

# ---- Method 5: Rapid disconnect counter ----
# Counts disconnects per 60-second window — a spike = deauth storm
watch_disconnect_rate() {
    log "[RATE] Starting disconnect rate monitor (threshold: 3/60s)..."
    COUNT=0
    WINDOW_START=$(date +%s)
    while true; do
        NOW=$(date +%s)
        ELAPSED=$((NOW - WINDOW_START))
        if [ "$ELAPSED" -ge 60 ]; then
            if [ "$COUNT" -ge 3 ]; then
                log "[ALERT][RATE] $COUNT disconnects in last 60s — DEAUTH STORM LIKELY"
            fi
            COUNT=0
            WINDOW_START=$NOW
        fi
        # Check current wifi state
        if ip link show "$WIFI_IF" 2>/dev/null | grep -q "state DOWN"; then
            COUNT=$((COUNT + 1))
        fi
        sleep 5
    done &
    log "[RATE] Rate watcher PID: $!"
}

# ---- Start all detection methods ----
create_monitor_iface
watch_dmesg
watch_wpa
watch_tcpdump
watch_disconnect_rate

log "[INIT] All monitors active."
log "[INIT] Tail this file to watch live: tail -f $LOGFILE"
log "[INIT] TIP: 'logcat | grep -i dhd' also shows driver-level WiFi events"
log "============================================"
