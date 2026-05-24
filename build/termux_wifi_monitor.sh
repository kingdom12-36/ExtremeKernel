#!/data/data/com.termux/files/usr/bin/bash
# ============================================================
# termux_wifi_monitor.sh — WiFi Attack Monitor (Termux)
# ExtremeKernel | Run this from Termux with root
#
# USAGE:
#   chmod +x termux_wifi_monitor.sh
#   ./termux_wifi_monitor.sh [command]
#
# COMMANDS:
#   start         Start passive monitoring (dmesg + wpa) — WiFi stays ON
#   monitor       Enable mon0 + full capture — WiFi is CUT during capture
#   stop          Kill all running monitor daemons
#   status        Show running daemons and last 20 log lines
#   log           Live tail the attack log
#
# IMPORTANT: Run as root — either prefix with 'su -c' or run inside 'su'
#
# EXAMPLE FROM TERMUX:
#   su
#   bash /sdcard/termux_wifi_monitor.sh start
#
# ============================================================

LOGFILE="/sdcard/logattacks.txt"
PIDFILE="/data/local/tmp/wifimon.pids"
MON_IF="mon0"
WIFI_IF="wlan0"

# Must be root
if [ "$(id -u)" != "0" ]; then
    echo "[ERROR] Must be run as root. Run: su -c 'bash $0 $*'"
    exit 1
fi

log() { echo "[$(date '+%Y-%m-%d %H:%M:%S')] $*" | tee -a "$LOGFILE"; }

usage() {
    echo ""
    echo "  termux_wifi_monitor.sh — ExtremeKernel WiFi Attack Detector"
    echo ""
    echo "  Commands:"
    echo "    start     Passive mode (dmesg + wpa events). WiFi stays ON."
    echo "    monitor   Full 802.11 capture via mon0. WiFi is DISCONNECTED."
    echo "    stop      Kill all running monitor daemons."
    echo "    status    Show daemon PIDs and last 20 log lines."
    echo "    log       Live tail: tail -f $LOGFILE"
    echo ""
}

# ──────────────────────────────────────────────────────────────
# PASSIVE START — dmesg + wpa_supplicant, WiFi stays active
# ──────────────────────────────────────────────────────────────
cmd_start() {
    echo "" > "$PIDFILE"  # reset pid list
    log "=============================="
    log "Passive WiFi Monitor Starting"
    log "Kernel: $(uname -r) | Device: $(getprop ro.product.model)"
    log "Methods: dmesg watcher + wpa_supplicant events + rate counter"
    log "WiFi connection stays UP."
    log "=============================="

    # dmesg watcher
    setsid sh -c "
        dmesg -w 2>/dev/null | while read LINE; do
            TS=\"\$(date '+%Y-%m-%d %H:%M:%S')\"
            case \"\$LINE\" in
                *deauth*|*DEAUTH*)          echo \"[\$TS] [ATTACK][DEAUTH] \$LINE\" >> $LOGFILE ;;
                *disassoc*|*DISASSOC*)      echo \"[\$TS] [ATTACK][DISASSOC] \$LINE\" >> $LOGFILE ;;
                *\"beacon loss\"*|*BEACON_LOSS*)
                                            echo \"[\$TS] [ATTACK][BEACON LOSS] \$LINE\" >> $LOGFILE ;;
                *\"reason=6\"*|*\"reason=7\"*)
                                            echo \"[\$TS] [ATTACK][REASON 6/7] \$LINE\" >> $LOGFILE ;;
                *disconnect*|*DISCONNECT*)  echo \"[\$TS] [INFO][DISCONNECT] \$LINE\" >> $LOGFILE ;;
            esac
        done
    " > /dev/null 2>&1 &
    DMESG_PID=$!
    echo $DMESG_PID >> "$PIDFILE"
    log "[dmesg watcher] PID: $DMESG_PID"

    # wpa_supplicant watcher
    for SOCK_DIR in "/data/vendor/wifi/wpa/sockets" "/data/misc/wifi/sockets"; do
        [ -d "$SOCK_DIR" ] || continue
        setsid sh -c "
            wpa_cli -p $SOCK_DIR -i $WIFI_IF -a /dev/null 2>/dev/null | while read EVENT; do
                TS=\"\$(date '+%Y-%m-%d %H:%M:%S')\"
                case \"\$EVENT\" in
                    *DISCONNECTED*) echo \"[\$TS] [ATTACK][WPA][DISCONNECTED] \$EVENT\" >> $LOGFILE ;;
                    *DEAUTH*)       echo \"[\$TS] [ATTACK][WPA][DEAUTH] \$EVENT\" >> $LOGFILE ;;
                    *DISASSOC*)     echo \"[\$TS] [ATTACK][WPA][DISASSOC] \$EVENT\" >> $LOGFILE ;;
                    *AUTH_FAILED*)  echo \"[\$TS] [SUSPECT][WPA][AUTH FAIL] \$EVENT\" >> $LOGFILE ;;
                esac
            done
        " > /dev/null 2>&1 &
        WPA_PID=$!
        echo $WPA_PID >> "$PIDFILE"
        log "[wpa watcher] PID: $WPA_PID"
        break
    done

    # Disconnect rate counter
    setsid sh -c "
        COUNT=0; PREV=up; WIN=\$(date +%s)
        while true; do
            sleep 5
            NOW=\$(date +%s)
            STATE=\$(cat /sys/class/net/$WIFI_IF/operstate 2>/dev/null || echo unknown)
            if [ \"\$STATE\" = down ] && [ \"\$PREV\" = up ]; then
                COUNT=\$((COUNT+1))
                echo \"[\$(date '+%Y-%m-%d %H:%M:%S')] [RATE] Disconnect #\$COUNT\" >> $LOGFILE
            fi
            PREV=\$STATE
            if [ \$((NOW - WIN)) -ge 60 ]; then
                [ \$COUNT -ge 3 ] && echo \"[\$(date '+%Y-%m-%d %H:%M:%S')] [ALERT] \$COUNT disconnects/60s — DEAUTH STORM\" >> $LOGFILE
                COUNT=0; WIN=\$NOW
            fi
        done
    " > /dev/null 2>&1 &
    RATE_PID=$!
    echo $RATE_PID >> "$PIDFILE"
    log "[rate watcher] PID: $RATE_PID"

    log "Passive monitoring active. Run: tail -f $LOGFILE"
    echo ""
    echo "  Monitoring in background. WiFi is STILL ON."
    echo "  Live log: tail -f $LOGFILE"
    echo "  Stop:     $0 stop"
}

# ──────────────────────────────────────────────────────────────
# MONITOR MODE — full 802.11 capture, WiFi WILL be cut
# ──────────────────────────────────────────────────────────────
cmd_monitor() {
    echo ""
    echo "  ⚠️  WARNING: WiFi (wlan0) will be DISCONNECTED."
    echo "     The BCM4375 cannot run managed + monitor at the same time."
    echo "     Mobile data / hotspot from another device is unaffected."
    echo ""
    printf "  Continue? [y/N] "
    read -r CONFIRM
    [ "$CONFIRM" = "y" ] || [ "$CONFIRM" = "Y" ] || { echo "Aborted."; exit 0; }

    echo "" > "$PIDFILE"

    log "=============================="
    log "Monitor Mode Starting (WiFi DISABLED)"
    log "=============================="

    # Disable wifi service, keep wlan0 up for the driver
    svc wifi disable 2>/dev/null
    sleep 2
    ip link set "$WIFI_IF" up 2>/dev/null

    # Create monitor interface
    iw dev "$WIFI_IF" interface add "$MON_IF" type monitor 2>/dev/null
    ip link set "$MON_IF" up 2>/dev/null

    if ip link show "$MON_IF" > /dev/null 2>&1; then
        log "[mon0] Monitor interface UP — capturing 802.11 management frames"

        setsid sh -c "
            tcpdump -i $MON_IF -nn -e -l 2>/dev/null | while read LINE; do
                TS=\"\$(date '+%Y-%m-%d %H:%M:%S')\"
                case \"\$LINE\" in
                    *Deauthentication*|*deauth*) echo \"[\$TS] [ATTACK][DEAUTH] \$LINE\" >> $LOGFILE ;;
                    *Disassociation*|*disassoc*) echo \"[\$TS] [ATTACK][DISASSOC] \$LINE\" >> $LOGFILE ;;
                    *Authentication*)            echo \"[\$TS] [AUTH] \$LINE\" >> $LOGFILE ;;
                esac
            done
        " > /dev/null 2>&1 &
        TCPD_PID=$!
        echo $TCPD_PID >> "$PIDFILE"
        log "[tcpdump] PID: $TCPD_PID"
    else
        log "[mon0] FAILED to create monitor interface."
        log "       Check: iw phy phy0 info | grep monitor"
        log "       Falling back to passive dmesg monitoring only."
    fi

    # Also run dmesg watcher alongside
    setsid sh -c "
        dmesg -w 2>/dev/null | while read LINE; do
            TS=\"\$(date '+%Y-%m-%d %H:%M:%S')\"
            case \"\$LINE\" in
                *deauth*|*DEAUTH*)     echo \"[\$TS] [ATTACK][DEAUTH] \$LINE\" >> $LOGFILE ;;
                *disassoc*|*DISASSOC*) echo \"[\$TS] [ATTACK][DISASSOC] \$LINE\" >> $LOGFILE ;;
            esac
        done
    " > /dev/null 2>&1 &
    echo $! >> "$PIDFILE"

    log "Monitor mode active. To restore WiFi: $0 stop"
    echo ""
    echo "  Capturing. Run: tail -f $LOGFILE"
    echo "  Stop + restore WiFi: $0 stop"
}

# ──────────────────────────────────────────────────────────────
# STOP
# ──────────────────────────────────────────────────────────────
cmd_stop() {
    if [ -f "$PIDFILE" ]; then
        while read -r PID; do
            [ -n "$PID" ] && kill "$PID" 2>/dev/null && echo "  Killed PID $PID"
        done < "$PIDFILE"
        rm -f "$PIDFILE"
    fi

    # Tear down monitor interface if it exists
    if ip link show "$MON_IF" > /dev/null 2>&1; then
        ip link set "$MON_IF" down 2>/dev/null
        iw dev "$MON_IF" del 2>/dev/null
        echo "  Removed $MON_IF"
    fi

    # Re-enable WiFi if it was disabled
    svc wifi enable 2>/dev/null
    echo "  WiFi re-enabled."
    log "[STOP] Monitor stopped. WiFi restored."
    echo "  Done. Log saved at: $LOGFILE"
}

# ──────────────────────────────────────────────────────────────
# STATUS
# ──────────────────────────────────────────────────────────────
cmd_status() {
    echo ""
    echo "=== Running monitor daemons ==="
    if [ -f "$PIDFILE" ]; then
        while read -r PID; do
            [ -n "$PID" ] && ps -p "$PID" -o pid,comm 2>/dev/null | tail -1
        done < "$PIDFILE"
    else
        echo "  No PID file found (not started, or started at boot via service.d)"
    fi

    echo ""
    echo "=== WiFi interface state ==="
    ip link show "$WIFI_IF" 2>/dev/null || echo "  $WIFI_IF not found"
    ip link show "$MON_IF" 2>/dev/null && echo "  $MON_IF is UP (monitor mode active)" || true

    echo ""
    echo "=== Last 20 log lines ($LOGFILE) ==="
    tail -20 "$LOGFILE" 2>/dev/null || echo "  Log file empty or not yet created."
    echo ""
}

# ──────────────────────────────────────────────────────────────
# DISPATCH
# ──────────────────────────────────────────────────────────────
case "${1:-}" in
    start)   cmd_start ;;
    monitor) cmd_monitor ;;
    stop)    cmd_stop ;;
    status)  cmd_status ;;
    log)     tail -f "$LOGFILE" ;;
    *)       usage ;;
esac
