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
# these rewrite them from the game's own AssetData_Binary.package. Both are safe to run again.
scripts=$(realpath "$(dirname "$0")/..")
game=$(dirname "$game_bin")
if [ ! -d "$server/data/serverdata" ]; then
	echo "No extracted game data yet. Once there is, run from $scripts:"
	echo "  python3 -I phase_gambits.py --game \"$game\" && python3 -I serverdata_fix.py --game \"$game\""
elif ! command -v python3 >/dev/null; then
	echo "python3 not found, so the game data was not repaired (see $scripts/serverdata_fix.py)." >&2
else
	log="$server/data_repair.log"
	: > "$log"
	for fix in phase_gambits.py serverdata_fix.py; do
		echo "Repairing game data with $fix (details in $log)"
		python3 -I "$scripts/$fix" --game "$game" >> "$log" ||
			echo "$fix failed, see $log; that part of the data stays as it was exported." >&2
	done
	grep -E '^[a-z][a-z ]*:' "$log" || true
fi

echo "Done. Hooks point at port $port."
echo "Start the server from $server with ./recap_server, then run RecapLauncher.exe --no-server under Proton."
