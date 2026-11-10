#!/usr/bin/env bash
# One-time setup on scaramanga, after the first tools/deploy_web.ps1 has
# copied this script to ~/kaizxo:
#
#   sudo bash ~/kaizxo/setup_server.sh
#
# Installs and starts kaizxo.service, then adds the game's hostname to the
# Cloudflare tunnel and restarts it. Safe to run again: it skips whatever is
# already done.
set -euo pipefail

HOST=kaizxo.christrewartha.com
PORT=8790
CONFIG=/etc/cloudflared/config.yml
HERE=$(dirname "$(readlink -f "$0")")

[ "$(id -u)" = 0 ] || { echo "Run this with sudo"; exit 1; }
[ -e "$HERE/site" ] || { echo "No site yet: run tools/deploy_web.ps1 first"; exit 1; }

install -m 644 "$HERE/kaizxo.service" /etc/systemd/system/kaizxo.service
systemctl daemon-reload
systemctl enable kaizxo
systemctl restart kaizxo
echo "kaizxo.service: $(systemctl is-active kaizxo)"

if grep -q "hostname: $HOST" "$CONFIG"; then
    echo "$HOST is already in $CONFIG"
    exit 0
fi

# The new rule goes just above the catch-all 404, which has to stay last
NEW=$(mktemp)
awk -v host="$HOST" -v port="$PORT" '
    /^ *- service: http_status:404/ && !done {
        print "  - hostname: " host
        print "    service: http://localhost:" port
        done = 1
    }
    { print }' "$CONFIG" > "$NEW"
grep -q "hostname: $HOST" "$NEW" || { echo "No catch-all rule found in $CONFIG: add $HOST by hand"; exit 1; }
cloudflared tunnel --config "$NEW" ingress validate

cp "$CONFIG" "$CONFIG.bak-$(date +%Y%m%d-%H%M%S)"
install -m 644 "$NEW" "$CONFIG"
rm "$NEW"
systemctl restart cloudflared
echo "Added $HOST to $CONFIG and restarted cloudflared"
