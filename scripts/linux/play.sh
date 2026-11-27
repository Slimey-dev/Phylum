#!/usr/bin/env bash
# Starts Darkspore through the ReCap launcher under Proton, against a server that is
# already running (start it from DarksporeBin/Server with ./recap_server).
#
#   scripts/linux/play.sh [path/to/DarksporeBin]
#
# PROTON=/path/to/proton picks a Proton build; the default is the newest GE-Proton,
# then Steam's Proton 10.0. The prefix is the Steam Demo's own (app 102830).
set -euo pipefail

steam_root=${STEAM_ROOT:-$HOME/.local/share/Steam}
game_bin=$(realpath "${1:-$steam_root/steamapps/common/Darkspore/DarksporeBin}")
app_id=102830

if [ -z "${PROTON:-}" ]; then
	PROTON=$(ls -d "$steam_root"/compatibilitytools.d/GE-Proton*/proton 2>/dev/null | sort -V | tail -n 1 || true)
	PROTON=${PROTON:-"$steam_root/steamapps/common/Proton 10.0/proton"}
fi
[ -x "$PROTON" ] || { echo "Proton not found at $PROTON; set PROTON=/path/to/proton" >&2; exit 1; }
[ -f "$game_bin/RecapLauncher.exe" ] || { echo "Run scripts/linux/install.sh first." >&2; exit 1; }

if ! ss -ltn 2>/dev/null | grep -q ':42127 '; then
	echo "The server is not running. Start it first: cd \"$game_bin/Server\" && ./recap_server" >&2
	exit 1
fi

export STEAM_COMPAT_CLIENT_INSTALL_PATH="$steam_root"
export STEAM_COMPAT_DATA_PATH="$steam_root/steamapps/compatdata/$app_id"
export SteamAppId=$app_id SteamGameId=$app_id
# Proton's xalia overlays launcher windows for controller navigation. Here it only
# competes with the game for focus.
export PROTON_USE_XALIA=0
mkdir -p "$STEAM_COMPAT_DATA_PATH"

echo "Using $PROTON"

# Switching to fullscreen crashes in Wine's XVidMode code under XWayland
# (xf86vm_free_modes assertion). With it off, Wine changes modes through XRandR.
if ! grep -qs '"UseXVidMode"="N"' "$STEAM_COMPAT_DATA_PATH/pfx/user.reg"; then
	"$PROTON" run reg add 'HKCU\Software\Wine\X11 Driver' /v UseXVidMode /t REG_SZ /d N /f
fi

# Keep the game windowed. Under XWayland, switching to fullscreen crashed or stopped
# mouse clicks from landing, and inside gamescope the intro movie froze on its first
# frame. DARKSPORE_RES=WIDTHxHEIGHT sets the window size. It must be a mode the monitor
# lists (see xrandr); the game ignores any other size and opens a small window instead.
prefs="$STEAM_COMPAT_DATA_PATH/pfx/drive_c/users/steamuser/AppData/Roaming/DarksporeData/Preferences/Preferences.prop"
if [ -f "$prefs" ]; then
	sed -i 's/^OptionFullScreen .*/OptionFullScreen 0/' "$prefs"
	if [ -n "${DARKSPORE_RES:-}" ]; then
		sed -i -e "s/^ScreenSizeWidth .*/ScreenSizeWidth ${DARKSPORE_RES%x*}/" \
			-e "s/^ScreenSizeHeight .*/ScreenSizeHeight ${DARKSPORE_RES#*x}/" "$prefs"
	fi
fi

# The launcher fails with error 267 unless it starts inside DarksporeBin.
cd "$game_bin"
exec "$PROTON" run ./RecapLauncher.exe --no-server
