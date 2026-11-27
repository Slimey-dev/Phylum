#!/usr/bin/env bash
# Installs the server built by darkspore_server/build.sh into a Darkspore install,
# next to the ReCap launcher and hooks, so the game can be played on Linux.
#
#   scripts/linux/install.sh <DarksporeBin> <extracted ReCap Ubuntu release>
#
# Accounts in Server/storage/user and extracted game data in Server/data/serverdata
# are kept across reinstalls.
set -euo pipefail

if [ $# -ne 2 ]; then
	echo "usage: $0 <path/to/DarksporeBin> <path/to/extracted/resurrection-capsule-ubuntu-x64>" >&2
	exit 2
fi

game_bin=$(realpath "$1")
recap=$(realpath "$2")
build=$(realpath "$(dirname "$0")/../../darkspore_server/build")
server="$game_bin/Server"

[ -f "$game_bin/Darkspore.exe" ] || { echo "No Darkspore.exe in $game_bin" >&2; exit 1; }
[ -f "$recap/RecapLauncher.exe" ] || { echo "No RecapLauncher.exe in $recap" >&2; exit 1; }
[ -x "$build/recap_server" ] || { echo "Build the server first: cd darkspore_server && ./build.sh" >&2; exit 1; }

if pgrep -x recap_server >/dev/null; then
	echo "Stop the running recap_server first." >&2
	exit 1
fi

port=8081
if [ -f "$server/config.xml" ]; then
	configured=$(sed -n 's:.*key="SERVER_HTTP_PORT" value="\([0-9]*\)".*:\1:p' "$server/config.xml")
	port=${configured:-$port}
fi

echo "Launcher and hooks -> $game_bin"
cp "$recap/RecapLauncher.exe" "$recap/RecapHooks.dll" "$game_bin/"
# The hooks must send the game's web traffic to the port the server listens on.
sed "s/^port = .*/port = $port/" "$recap/RecapHooks.toml" > "$game_bin/RecapHooks.toml"

echo "Extraction tools -> $server"
mkdir -p "$server/storage/user"
for f in dbpf_unpacker recap_parser unluac reg_cnv.txt reg_file.txt reg_property.txt \
	reg_simulator.txt reg_simulator_stub.txt reg_type.txt; do
	cp "$recap/Server/$f" "$server/"
done
chmod +x "$server/dbpf_unpacker" "$server/recap_parser" "$server/unluac"

echo "Server -> $server"
binary=$(readlink -f "$build/recap_server")
cp "$binary" "$server/"
ln -sf "$(basename "$binary")" "$server/recap_server"
cp -r "$build/data" "$server/"
[ -d "$build/storage/template_png" ] && cp -r "$build/storage/template_png" "$server/storage/"
[ -d "$build/storage/www" ] && cp -r "$build/storage/www" "$server/storage/"

# recap_parser's export misses the AI's gambits and breaks some nouns, classes and levels;
# these rewrite them from the game's own AssetData_Binary.package. navmesh_extract.py adds the
# levels' navmeshes from Levels.package. All are safe to run again.
scripts=$(realpath "$(dirname "$0")/..")
game=$(dirname "$game_bin")
data="$server/data/serverdata"
package="$game/Data/AssetData_Binary.package"
# The server extracts the game data on its first start and writes version_bin.txt when it is done.
# The folder itself always exists, since the build's data is copied in above.
if [ ! -f "$data/version_bin.txt" ] || [ ! -d "$data/noun" ] || [ ! -d "$data/phase" ]; then
	echo "The game data is not extracted yet. Start the server once, stop it, then run:"
	echo "  python3 -I \"$scripts/phase_gambits.py\" --game \"$game\" --phases \"$data/phase\""
	echo "  python3 -I \"$scripts/serverdata_fix.py\" --game \"$game\" --data \"$data\""
	echo "  python3 -I \"$scripts/navmesh_extract.py\" --game \"$game\" --data \"$data\""
	echo "or run this script again."
elif ! command -v python3 >/dev/null; then
	echo "python3 not found, so the game data was not repaired (see $scripts/serverdata_fix.py)." >&2
else
	log="$server/data_repair.log"
	: > "$log"
	echo "Repairing game data (details in $log)"
	python3 -I "$scripts/phase_gambits.py" --game "$game" --package "$package" --phases "$data/phase" >> "$log" 2>&1 ||
		echo "phase_gambits.py failed, see $log; the AI's gambits stay as they were exported." >&2
	python3 -I "$scripts/serverdata_fix.py" --game "$game" --package "$package" --data "$data" >> "$log" 2>&1 ||
		echo "serverdata_fix.py failed, see $log; that part of the data stays as it was exported." >&2
	# The levels' navmeshes (Levels.package) and the layer radii, for the server's paths; written to data/serverdata/navmesh.
	python3 -I "$scripts/navmesh_extract.py" --game "$game" --package "$package" --data "$data" >> "$log" 2>&1 ||
		echo "navmesh_extract.py failed, see $log; the server has no navmesh and walks in straight lines." >&2
	grep -E '^[a-z][a-z ]*:' "$log" || true
fi

echo "Done. Hooks point at port $port."
echo "Start the server from $server with ./recap_server, then run RecapLauncher.exe --no-server under Proton."
