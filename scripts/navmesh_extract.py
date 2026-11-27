"""Extract the levels' BabelFlux NavPower navmeshes for the server.

The client walks every creature on its own navmesh, one .bfx per level. The server needs the same
meshes to plan the same paths. This reads them from the game's Levels.package and writes

  <serverdata>/navmesh/<name>.bfx            one per level, named as in the level's <navMesh> entry
  <serverdata>/navmesh/navpowertuning.json   the seven NavPowerTuning layer radii

Which meshes: every data/serverdata/level*/<name>.Level.xml (any case) holds
<navMesh>X!X.bfx</navMesh>. The mesh is the Levels.package entry of type 0x1999AE0B whose group and
instance are both the 32-bit FNV-1a of lower(X). It is RefPack-compressed in the package and is
written decompressed. The decompressed file starts with the envelope the server checks: 0, 2,
size - 0x18 (zelems_1.bfx, 634772 bytes, has SHA-256 7d3cee6a...).

The layer radii (the footprint radius picks the layer a creature walks on) are the NavPowerTuning
asset, type 0xBBA5400D instance 0x57572DAC in AssetData_Binary.package, 232 bytes:
  +0 float 45.0, +4 pointer, +8 row count (7), then 7 rows of 24 bytes from +0x0C: name pointer,
  voxel size, radius, height, step, slope; then the 7 NUL-terminated names ("1x1", "1.5x1.5", ...).
If that does not parse, the known radii 0.71 1.06 1.24 1.41 1.59 1.77 3.89 are written. The
data/serverdata/navpowertuning/global.NavPowerTuning.xml that recap_parser exports is broken and is
not read.

Running again changes nothing. Files are written whole or not at all.

  python3 -I scripts/navmesh_extract.py --game "<Darkspore folder>" [--package P] [--data DIR] [--dry-run]
"""
import argparse
import glob
import hashlib
import json
import os
import re
import struct
import sys

# python -I leaves this folder off sys.path; the RefPack code is shared with phase_gambits.py.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from phase_gambits import DEFAULT_GAME, fnv, refpack  # noqa: E402

NAVMESH_TYPE = 0x1999AE0B
TUNING_TYPE = 0xBBA5400D
TUNING_INSTANCE = 0x57572DAC
TUNING_SIZE = 232
TUNING_ROWS = 7
TUNING_FIRST_ROW = 0x0C
FALLBACK_RADII = (0.71, 1.06, 1.24, 1.41, 1.59, 1.77, 3.89)
FALLBACK_NAMES = ("1x1", "1.5x1.5", "1.75x1.75", "2x2", "2.25x2.25", "2.5x2.5", "5.5x5.5")
NAVMESH_PATTERN = re.compile(r"<navMesh>\s*([^<!\s]+)!([^<\s]+?)\.bfx\s*</navMesh>", re.IGNORECASE)


class Package:
    """The index of a DBPF 3 package; entries are read from the file on demand."""

    def __init__(self, path):
        self.path = path
        self.entries = {}
        with open(path, "rb") as handle:
            header = handle.read(0x60)
            if len(header) < 0x48 or header[:4] != b"DBPF":
                raise ValueError(f"{path}: not a DBPF package")
            count = struct.unpack_from("<I", header, 0x24)[0]
            index_size = struct.unpack_from("<I", header, 0x2C)[0]
            position = struct.unpack_from("<Q", header, 0x40)[0]
            handle.seek(0, os.SEEK_END)
            end = handle.tell()
            if position >= end:
                raise ValueError(f"{path}: index past the end of the file")
            handle.seek(position)
            index = handle.read(index_size if 0 < index_size <= end - position else end - position)
        flags = struct.unpack_from("<I", index, 0)[0]
        cursor = 4
        shared = {}
        for bit, name in ((1, "type"), (2, "group"), (4, "high")):
            if flags & bit:
                shared[name] = struct.unpack_from("<I", index, cursor)[0]
                cursor += 4
        for _ in range(count):
            fields = {}
            for name in ("type", "group", "high"):
                if name in shared:
                    fields[name] = shared[name]
                else:
                    fields[name] = struct.unpack_from("<I", index, cursor)[0]
                    cursor += 4
            instance, offset, stored, size, compression, _flags = struct.unpack_from("<IIIIHH", index, cursor)
            cursor += 20
            key = (fields["type"], fields["group"], instance)
            self.entries[key] = (offset, stored & 0x7FFFFFFF, size, compression)

    def read(self, kind, group, instance):
        """The decompressed payload of an asset, or None if the package has none."""
        entry = self.entries.get((kind, group, instance))
        if entry is None:
            return None
        offset, stored, size, compression = entry
        with open(self.path, "rb") as handle:
            handle.seek(offset)
            raw = handle.read(stored)
        if len(raw) != stored:
            raise ValueError(f"{self.path}: asset {instance:08X} runs past the end of the file")
        return refpack(raw, size) if compression == 0xFFFF else raw


def write_bytes(path, content, dry_run):
    """Write a file whole or not at all, and not at all if it already holds this. True if it changed."""
    try:
        with open(path, "rb") as handle:
            if handle.read() == content:
                return False
    except FileNotFoundError:
        pass
    if dry_run:
        return True
    temporary = path + ".tmp"
    with open(temporary, "wb") as handle:
        handle.write(content)
        handle.flush()
        os.fsync(handle.fileno())
    os.replace(temporary, path)
    return True


def level_meshes(data):
    """{lower(name): name} for every navmesh a level file names, and the number of level files read."""
    names = {}
    files = 0
    for folder in sorted(glob.glob(os.path.join(glob.escape(data), "level*"))):
        if not os.path.isdir(folder):
            continue
        for entry in sorted(os.listdir(folder)):
            if not entry.lower().endswith(".level.xml"):
                continue
            files += 1
            with open(os.path.join(folder, entry), "rb") as handle:
                text = handle.read().decode("latin-1")
            for match in NAVMESH_PATTERN.finditer(text):
                if match.group(1).lower() != match.group(2).lower():
                    print(f"warning: {entry}: navMesh {match.group(1)}!{match.group(2)}.bfx, using {match.group(2)}",
                          file=sys.stderr)
                names.setdefault(match.group(2).lower(), match.group(2))
    return names, files


def valid_envelope(payload):
    return (len(payload) > 0x30 and struct.unpack_from("<III", payload, 0) == (0, 2, len(payload) - 0x18))


def extract_meshes(levels, data, dry_run):
    names, files = level_meshes(data)
    folder = os.path.join(data, "navmesh")
    if not dry_run:
        os.makedirs(folder, exist_ok=True)
    written = unchanged = 0
    missing = []
    bad = []
    for lower, name in sorted(names.items()):
        key = fnv(lower)
        try:
            payload = levels.read(NAVMESH_TYPE, key, key)
        # RefPack raises IndexError on cut-short or damaged data; one bad mesh must not stop the rest.
        except (ValueError, IndexError) as error:
            print(f"error: {name}: {error}", file=sys.stderr)
            bad.append(name)
            continue
        if payload is None:
            missing.append(name)
            continue
        if not valid_envelope(payload):
            print(f"error: {name}: the decompressed mesh has a wrong header, not written", file=sys.stderr)
            bad.append(name)
            continue
        if lower == "zelems_1" and not hashlib.sha256(payload).hexdigest().startswith("7d3cee6a"):
            print("warning: zelems_1.bfx does not have the SHA-256 7d3cee6a... of game build 5.3.0.103",
                  file=sys.stderr)
        if write_bytes(os.path.join(folder, name + ".bfx"), payload, dry_run):
            written += 1
        else:
            unchanged += 1
    print(f"navmesh: {files} level files, {len(names)} meshes: {written} written, {unchanged} unchanged, "
          f"{len(missing)} not in the package, {len(bad)} bad")
    if missing:
        print("navmesh: not in the package: " + ", ".join(missing))
    return not bad


def parse_tuning(payload):
    """The 7 layer rows of the NavPowerTuning asset, or None if it is not what we expect."""
    if payload is None or len(payload) != TUNING_SIZE:
        return None
    if struct.unpack_from("<I", payload, 8)[0] != TUNING_ROWS:
        return None
    names = payload[TUNING_FIRST_ROW + TUNING_ROWS * 24:].split(b"\0")[:TUNING_ROWS]
    layers = []
    previous = 0.0
    for row in range(TUNING_ROWS):
        _name, voxel, radius, height, step, slope = struct.unpack_from("<I5f", payload, TUNING_FIRST_ROW + row * 24)
        if not (0.1 <= radius < 100.0 and radius > previous and 0.0 < voxel < 10.0):
            return None
        previous = radius
        name = names[row].decode("latin-1") if row < len(names) and names[row] else FALLBACK_NAMES[row]
        layers.append({"name": name, "voxelSize": round(voxel, 4), "radius": round(radius, 4),
                       "height": round(height, 4), "step": round(step, 4), "slope": round(slope, 4)})
    return layers


def extract_tuning(assets, data, dry_run):
    layers = None
    source = "fallback"
    try:
        package = Package(assets)
        # The asset's group is not documented: take the one with this type and instance.
        for kind, group, instance in package.entries:
            if kind == TUNING_TYPE and instance == TUNING_INSTANCE:
                layers = parse_tuning(package.read(kind, group, instance))
                break
    except (OSError, ValueError, IndexError, struct.error) as error:
        print(f"navmesh: {assets}: {error}", file=sys.stderr)
    if layers is not None:
        source = "AssetData_Binary.package"
    else:
        print("navmesh: NavPowerTuning did not parse, writing the known radii", file=sys.stderr)
        layers = [{"name": name, "radius": radius} for name, radius in zip(FALLBACK_NAMES, FALLBACK_RADII)]
    text = json.dumps({"source": source, "layers": layers}, indent="\t") + "\n"
    folder = os.path.join(data, "navmesh")
    if not dry_run:
        os.makedirs(folder, exist_ok=True)
    changed = write_bytes(os.path.join(folder, "navpowertuning.json"), text.encode("ascii"), dry_run)
    radii = " ".join(f"{layer['radius']:g}" for layer in layers)
    print(f"navmesh: layer radii {radii} ({source}, {'written' if changed else 'unchanged'})")
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--game", default=DEFAULT_GAME, help="the Darkspore install folder")
    parser.add_argument("--package", help="the NavPowerTuning package, default: <game>/Data/AssetData_Binary.package")
    parser.add_argument("--levels", help="default: <game>/Data/Levels.package")
    parser.add_argument("--data", help="default: <game>/DarksporeBin/Server/data/serverdata")
    parser.add_argument("--dry-run", action="store_true", help="report what would change and write nothing")
    arguments = parser.parse_args()
    arguments.package = arguments.package or os.path.join(arguments.game, "Data", "AssetData_Binary.package")
    arguments.levels = arguments.levels or os.path.join(arguments.game, "Data", "Levels.package")
    arguments.data = arguments.data or os.path.join(arguments.game, "DarksporeBin", "Server", "data", "serverdata")

    try:
        levels = Package(arguments.levels)
    except (OSError, ValueError, struct.error) as error:
        print(f"navmesh: {error}", file=sys.stderr)
        return 1
    results = [
        extract_meshes(levels, arguments.data, arguments.dry_run),
        extract_tuning(arguments.package, arguments.data, arguments.dry_run),
    ]
    if arguments.dry_run:
        print("dry run: nothing was written")
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
