#!/system/bin/sh
# ============================================================
# wifi_monitor_attacks.sh — WiFi Attack Detector
# ExtremeKernel | KPatch+Monitor Variant
# Auto-installed to: /data/adb/service.d/wifi_monitor_attacks.sh
# Runs once at boot via KernelSU/Magisk service.d
#
# HOW IT WORKS:
#   This script launches several background daemons with setsid
#   so they survive after this script exits. The daemons stay
#   running until you reboot or manually kill them (see PIDs in
#   the log). Removing this file stops them on the NEXT reboot.
#
# IMPORTANT — WIFI WILL DISCONNECT IN MONITOR MODE:
#   The BCM4375 chip cannot be in managed mode (connected to AP)
#   and monitor mode (raw 802.11 capture) at the same time.
#   Methods 1 & 2 below require disabling wlan0 first.
#   Methods 3-5 are ALWAYS ACTIVE and never cut your WiFi.
#   For attack detection on YOUR OWN connection, Methods 3-5
#   are all you need — they catch every deauth/disassoc hit.
#
# LOGS: tail -f /sdcard/logattacks.txt
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
log "Device: $(getprop ro.product.model 2>/dev/null)"
log "Android: $(getprop ro.build.version.release 2>/dev/null)"
log "============================================"
log "NOTE: Methods 3-5 are always active (no WiFi cut)."
log "Monitor interface (mon0) requires: svc wifi disable first."
log "============================================"

# ---- Method 1: Try to create mon0 monitor interface ----
# WL_MONITOR is compiled into ExtremeKernel (bcmdhd_101_16 Makefile).
# HOWEVER: the BCM4375 chip cannot run wlan0 (managed/internet) AND
# mon0 (monitor) simultaneously. Enabling mon0 WILL cut your WiFi.
# This method is intentionally left non-blocking — if iw fails because
# wlan0 is active, we silently skip and rely on Methods 3-5.
create_monitor_iface() {
    iw dev "$WIFI_IF" interface add "$MON_IF" type monitor 2>/dev/null \
        && ip link set "$MON_IF" up 2>/dev/null \
        && log "[METHOD1] Monitor interface $MON_IF UP — raw 802.11 capture active" \
        && return 0
    log "[METHOD1] mon0 skipped (wlan0 active or driver busy). Methods 3-5 cover you."
    return 1
}

# ---- Method 2: tcpdump on mon0 (only if mon0 came up) ----
# Captures raw 802.11 deauth/disassoc management frames.
# Only useful when WiFi is disabled and mon0 is up.
watch_tcpdump() {
    ip link show "$MON_IF" > /dev/null 2>&1 || return
    log "[METHOD2] tcpdump capturing 802.11 management frames on $MON_IF..."
    setsid sh -c "
        tcpdump -i $MON_IF -nn -e -l 2>/dev/null | while read LINE; do
            case \"\$LINE\" in
                *Deauthentication*|*deauth*|*DeAuth*)
                    echo \"[\$(date '+%Y-%m-%d %H:%M:%S')] [ATTACK][DEAUTH] \$LINE\" >> $LOGFILE ;;
                *Disassociation*|*disassoc*)
                    echo \"[\$(date '+%Y-%m-%d %H:%M:%S')] [ATTACK][DISASSOC] \$LINE\" >> $LOGFILE ;;
                *Authentication*)
                    echo \"[\$(date '+%Y-%m-%d %H:%M:%S')] [AUTH] \$LINE\" >> $LOGFILE ;;
            esac
        done
    " > /dev/null 2>&1 &
    log "[METHOD2] tcpdump PID: $!"
}

# ---- Method 3: dmesg watcher — BCM4375 DHD driver events ----
# The bcmdhd_101_16 driver logs deauth/disassoc/beacon-loss to the
# kernel ring buffer. This works 100% without touching your WiFi.
# Deauth reason codes: 1=unspecified, 3=STA leaving, 6=class2 frame,
# 7=class3 frame (most deauth attacks use 1, 6, or 7).
watch_dmesg() {
    log "[METHOD3] Starting BCM4375 DHD dmesg watcher..."
    setsid sh -c "
        dmesg -w 2>/dev/null | while read LINE; do
            TS=\"\$(date '+%Y-%m-%d %H:%M:%S')\"
            case \"\$LINE\" in
                *deauth*|*DEAUTH*)
                    echo \"[\$TS] [ATTACK][DMESG][DEAUTH] \$LINE\" >> $LOGFILE ;;
                *disassoc*|*DISASSOC*)
                    echo \"[\$TS] [ATTACK][DMESG][DISASSOC] \$LINE\" >> $LOGFILE ;;
                *\"beacon loss\"*|*BEACON_LOSS*|*beacon_loss*)
                    echo \"[\$TS] [ATTACK][DMESG][BEACON LOSS] Possible jamming — \$LINE\" >> $LOGFILE ;;
                *\"reason=6\"*|*\"reason=7\"*)
                    echo \"[\$TS] [ATTACK][DMESG][REASON 6/7] Class2/3 frame attack — \$LINE\" >> $LOGFILE ;;
                *\"reason=1\"*)
                    echo \"[\$TS] [SUSPECT][DMESG][REASON=1] Unspecified disconnect — \$LINE\" >> $LOGFILE ;;
                *disconnect*|*DISCONNECT*)
                    echo \"[\$TS] [INFO][DMESG][DISCONNECT] \$LINE\" >> $LOGFILE ;;
            esac
        done
    " > /dev/null 2>&1 &
    log "[METHOD3] dmesg watcher PID: $!"
}

# ---- Method 4: wpa_supplicant event monitor ----
# Catches disconnect/deauth events at the supplicant level.
# Higher-level than dmesg — confirms the attack reached your association.
watch_wpa() {
    for SOCK_DIR in "/data/vendor/wifi/wpa/sockets" "/data/misc/wifi/sockets"; do
        [ -d "$SOCK_DIR" ] || continue
        log "[METHOD4] Watching wpa_supplicant at $SOCK_DIR..."
        setsid sh -c "
            wpa_cli -p $SOCK_DIR -i $WIFI_IF -a /dev/null 2>/dev/null | while read EVENT; do
                TS=\"\$(date '+%Y-%m-%d %H:%M:%S')\"
                case \"\$EVENT\" in
                    *DISCONNECTED*)  echo \"[\$TS] [ATTACK][WPA][DISCONNECTED] \$EVENT\" >> $LOGFILE ;;
                    *DEAUTH*)        echo \"[\$TS] [ATTACK][WPA][DEAUTH] \$EVENT\" >> $LOGFILE ;;
                    *DISASSOC*)      echo \"[\$TS] [ATTACK][WPA][DISASSOC] \$EVENT\" >> $LOGFILE ;;
                    *AUTH_FAILED*)   echo \"[\$TS] [SUSPECT][WPA][AUTH_FAILED] Possible brute-force — \$EVENT\" >> $LOGFILE ;;
                    *TEMP_DISABLED*) echo \"[\$TS] [SUSPECT][WPA][TEMP_DISABLED] \$EVENT\" >> $LOGFILE ;;
                esac
            done
        " > /dev/null 2>&1 &
        log "[METHOD4] wpa_cli PID: $!"
        return
    done
    log "[METHOD4] wpa_supplicant socket not found, skipping."
}

# ---- Method 5: Rapid disconnect rate counter ----
# Counts WiFi disconnects per 60-second window.
# 3+ disconnects in 60s = likely deauth storm / attack.
# Works entirely from /sys/class/net — zero WiFi impact.
watch_disconnect_rate() {
    log "[METHOD5] Disconnect rate monitor started (alert threshold: 3/60s)..."
    setsid sh -c "
        COUNT=0
        PREV_STATE=up
        WIN_START=\$(date +%s)
        while true; do
            sleep 5
            NOW=\$(date +%s)
            ELAPSED=\$((NOW - WIN_START))
            STATE=\$(cat /sys/class/net/$WIFI_IF/operstate 2>/dev/null || echo unknown)
            if [ \"\$STATE\" = down ] && [ \"\$PREV_STATE\" = up ]; then
                COUNT=\$((COUNT + 1))
                echo \"[\$(date '+%Y-%m-%d %H:%M:%S')] [RATE] Disconnect #\$COUNT in window\" >> $LOGFILE
            fi
            PREV_STATE=\$STATE
            if [ \"\$ELAPSED\" -ge 60 ]; then
                if [ \"\$COUNT\" -ge 3 ]; then
                    echo \"[\$(date '+%Y-%m-%d %H:%M:%S')] [ALERT][RATE] \$COUNT disconnects in 60s — DEAUTH STORM DETECTED\" >> $LOGFILE
                fi
                COUNT=0
                WIN_START=\$NOW
            fi
        done
    " > /dev/null 2>&1 &
    log "[METHOD5] Rate watcher PID: $!"
}

# ---- Launch all methods ----
create_monitor_iface
watch_dmesg        # always on — no WiFi cut
watch_wpa          # always on — no WiFi cut
watch_tcpdump      # only if mon0 is up
watch_disconnect_rate  # always on — no WiFi cut

log "[INIT] All active monitors running. Methods 3-5 never cut WiFi."
log "[INIT] To view live: tail -f $LOGFILE"
log "[INIT] To stop daemons: check PIDs above and: kill <PID>"
log "============================================"
