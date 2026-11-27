"""Repair the nouns, non-player classes and level markerset lists that recap_parser exports wrongly.

The installer's recap_parser gets three asset types wrong. This reads the binary assets in
AssetData_Binary.package and rewrites what is wrong in data/serverdata:

  noun/            A noun whose eliteAssetIds array is not empty loses every field after it. With
                   creatureThumbnailData the ids come out nested and run on into the string table;
                   without it the array is skipped and physicsKey, aiDefinition, toonType and the
                   effects are read from the ids. That is 35 nouns, among them ZelemBasicRanged,
                   Boomer and the Cryos minions, and without isCreature and hasLocomotion the server
                   gives them no health, locomotion or AI. Their XML is rewritten from eliteAssetIds
                   on; the part before it is checked against the binary and kept as it is.
  nonplayerclass/  challengeValue, the class's cost to the director, is left out; npcRank is a copy
                   of mNPCType; dropType entries are nested one level too deep. All three are set
                   from the binary; eliteAffix is checked and added where it is missing.
  level/           every <markersets> entry gets its set's variant <group> and <weight>. The client
                   loads each set whose group is 'none' and one set per other group, picked by weight
                   (Darkspore.exe 0x9D6340). The level asset only lists the sets; group and weight
                   come from each Markerset asset.

Binary layout (Darkspore.exe 5.3.0.103, from the client's reflection tables: Noun field descriptors
0x116B4B0-0x116CBF0, NonPlayerClass 0x1166280-0x1166940, stride 0x60, in registration order):
  An asset is its struct image, then the data behind each set pointer, field by field in
  registration order and depth first: a string; a nested struct's image, then its own pointer
  data; an array's elements, then each element's pointer data. A Key is 16 bytes: fnv of its name
  +0, type id +4, fnv of its group +8, and a pointer to its "group!name.type" string +12. LAYOUT
  below lists the pointer fields of every struct a noun can hold. The order inside the nested
  structs comes from AssetData.Parser and is approximate; every one of the 4971 nouns must use up
  its asset exactly, or nothing is written.
  Noun 0x1E0 bytes: eliteAssetIds +0xB0 (count +0xB4, uint64 ids), physicsType +0xB8,
    density +0xBC, physicsKey Key +0xCC, affectsNavMesh +0xD0, dynamicWall +0xD1,
    aiDefinition +0xD4, hasNetwork/Camera/Combatant/Locomotion +0xD8..+0xDB, locomotionType +0xDC,
    spawnTeamId +0xE0, isIslandMarker +0xE4, componentData +0xFC (10 pointers),
    locomotionTuning +0x130, toonType Key +0x144, isFlora/isMineral/isCreature/isPlayer/isSpawned
    +0x148..+0x14C, eight effect Keys up to +0x1DC.
  NonPlayerClass 0x7C bytes: name +0x10 and description +0x50 (text pointer, id pointer),
    challengeValue +0x24, dropType +0x28 (count +0x2C, uint32), mNPCType +0x44, npcRank +0x48,
    longDescription +0x68 (count +0x6C, 20 bytes each), eliteAffix +0x70 (count +0x74, 12 bytes:
    affix asset, minDifficulty, maxDifficulty).
  Markerset 0x28 bytes: markers +0 (count +4), group Key +0x08 (string pointer +0x14),
    weight float +0x18, condition +0x20 (count +0x24). The group string is the last data in the
    asset, before the conditions. It can follow other data with no NUL between, so it is found by
    its hash.
  Level 0x98 bytes: markersets +0 (count +4), each a 4-byte Markerset asset pointer.

Each decoder is checked before anything is written. Every field it decodes is compared with
recap_parser's export of all nouns without eliteAssetIds, all classes and all markersets, and a
section is left alone if one differs. Where recap_parser is known to misread a field (NOUN_MISREADS,
NPC_MISREADS), a file holding exactly what recap_parser makes of the binary is counted apart. The
nouns' hasCameraComponent is one: recap_parser writes whether aiDefinition is set, the client's flag
(+0xD9) is false in every noun, and the rewritten nouns get the client's value. The server does not
read it. Running the script again changes nothing.

  python3 -I scripts/serverdata_fix.py --game "<Darkspore folder>" [--package P] [--data DIR] [--dry-run]
"""
import argparse
import collections
import os
import re
import struct
import sys
import xml.etree.ElementTree as ElementTree
from xml.sax.saxutils import escape

# python -I leaves this folder off sys.path; the DBPF and RefPack code is shared with phase_gambits.py.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from phase_gambits import DEFAULT_GAME, Cursor, fnv, refpack  # noqa: E402

NOUN_TYPE = 0x76A8F7D8
NONPLAYERCLASS_TYPE = 0xD117AFCA
MARKERSET_TYPE = 0xA11D3144
LEVEL_TYPE = 0xB9193960

# Pointer fields per struct, in the order their data follows in the asset:
# (name, kind, offset[, struct or element size]). Kinds: "string" (Key, asset or char*),
# "struct" (nullable struct), "array" (pointer, count at +4), "inline" (struct held in place).
LAYOUT = {
    "Noun": (0x1E0, [
        ("modelKey", "string", 0x24), ("prefab", "string", 0x10), ("levelEditorModelKey", "string", 0x34),
        ("voice", "string", 0x60), ("foot", "string", 0x70), ("flightSound", "string", 0x80),
        ("gfxStates", "struct", 0x84, "cGameObjectGfxStates"), ("doorDef", "struct", 0x88, "cDoorDef"),
        ("switchDef", "struct", 0x8C, "cSwitchDef"), ("pressureSwitchDef", "struct", 0x90, "cPressureSwitchDef"),
        ("crystalDef", "struct", 0x94, "CrystalDef"),
        ("npcClassData", "string", 0xA0), ("playerClassData", "string", 0xA4),
        ("characterAnimationData", "string", 0xA8),
        ("creatureThumbnailData", "struct", 0xAC, "cThumbnailCaptureParameters"),
        ("eliteAssetIds", "array", 0xB0, 8),
        ("physicsKey", "string", 0xCC), ("aiDefinition", "string", 0xD4),
        ("activateFnNamespace", "string", 0xE8), ("tickFnNamespace", "string", 0xEC),
        ("deactivateFnNamespace", "string", 0xF0), ("startFnNamespace", "string", 0xF4),
        ("endFnNamespace", "string", 0xF8),
        ("triggerVolume", "struct", 0x124, "TriggerVolumeDef"), ("projectile", "struct", 0x128, "ProjectileDef"),
        ("orbit", "struct", 0x12C, "OrbitDef"), ("locomotionTuning", "struct", 0x130, "LocomotionTuning"),
        ("gravityData", "string", 0x134),
        ("componentData", "inline", 0xFC, "SharedComponentData"),
        ("toonType", "string", 0x144),
        ("modelEffect", "string", 0x15C), ("removalEffect", "string", 0x16C),
        ("meleeDeathEffect", "string", 0x18C), ("meleeCritEffect", "string", 0x19C),
        ("energyDeathEffect", "string", 0x1AC), ("energyCritEffect", "string", 0x1BC),
        ("plasmaDeathEffect", "string", 0x1CC), ("plasmaCritEffect", "string", 0x1DC)]),
    "cThumbnailCaptureParameters": (0x6C, []),
    "cGameObjectGfxStates": (0x8, [("state", "array", 0, "cGameObjectGfxStateData")]),
    "cGameObjectGfxStateData": (0x38, [
        ("name", "string", 0xC), ("model", "string", 0x1C), ("animation", "string", 0x2C), ("prefab", "string", 0x30)]),
    "cNewGfxState": (0x28, [("prefab", "string", 0), ("model", "string", 0x10), ("animation", "string", 0x20)]),
    "cDoorDef": (0x18, [
        ("graphicsState_open", "struct", 0, "cNewGfxState"), ("graphicsState_closed", "struct", 8, "cNewGfxState")]),
    "cSwitchDef": (0xC, [
        ("graphicsState_unpressed", "struct", 0, "cNewGfxState"), ("graphicsState_pressing", "struct", 4, "cNewGfxState"),
        ("graphicsState_pressed", "struct", 8, "cNewGfxState")]),
    "cPressureSwitchDef": (0x28, [
        ("graphicsState_unpressed", "struct", 0, "cNewGfxState"), ("graphicsState_pressing", "struct", 4, "cNewGfxState"),
        ("graphicsState_pressed", "struct", 8, "cNewGfxState")]),
    "CrystalDef": (0x18, [("modifier", "string", 0xC)]),
    "TriggerVolumeDef": (0x88, [
        ("onEnter", "string", 0xC), ("onExit", "string", 0x1C), ("onStay", "string", 0x2C),
        ("events", "struct", 0x30, "TriggerVolumeEvents"), ("luaCallbackOnEnter", "string", 0x54),
        ("luaCallbackOnExit", "string", 0x58), ("luaCallbackOnStay", "string", 0x5C)]),
    "TriggerVolumeEvents": (0x20, [("onEnterEvent", "string", 0xC), ("onExitEvent", "string", 0x1C)]),
    "ProjectileDef": (0xC, [
        ("creatureCollisionVolume", "struct", 0, "CollisionVolumeDef"),
        ("otherCollisionVolume", "struct", 4, "CollisionVolumeDef")]),
    "CollisionVolumeDef": (0x14, []),
    "OrbitDef": (0xC, []),
    "LocomotionTuning": (0xC, []),
    "SharedComponentData": (0x28, [
        ("audioTrigger", "struct", 0, "AudioTriggerDef"), ("teleporter", "struct", 4, "TeleporterDef"),
        ("eventListenerDef", "struct", 8, "EventListenerDef"), ("spawnPointDef", "struct", 0x10, "SpawnPointDef"),
        ("spawnTrigger", "struct", 0xC, "SpawnTriggerDef"), ("interactable", "struct", 0x14, "InteractableDef"),
        ("defaultGfxState", "struct", 0x18, "GameObjectGfxStateTuning"), ("combatant", "struct", 0x1C, "CombatantDef"),
        ("triggerComponent", "struct", 0x20, "TriggerVolumeComponentDef"),
        ("spaceshipSpawnPoint", "struct", 0x24, "SpaceshipSpawnPointDef")]),
    "AudioTriggerDef": (0x20, [("sound", "string", 0x10), ("triggerVolume", "struct", 0x1C, "TriggerVolumeDef")]),
    "TeleporterDef": (0xC, [("triggerVolume", "struct", 4, "TriggerVolumeDef")]),
    "EventListenerDef": (0x8, [("listener", "array", 0, "EventListenerData")]),
    "EventListenerData": (0x28, [("event", "string", 0xC), ("callback", "string", 0x1C), ("luaCallback", "string", 0x24)]),
    "SpawnTriggerDef": (0x1C, [("triggerVolume", "struct", 0, "TriggerVolumeDef"), ("deathEvent", "string", 0x10)]),
    "SpawnPointDef": (0x8, []),
    "InteractableDef": (0x48, [
        ("interactableAbility", "string", 0x10), ("startInteractEvent", "string", 0x20),
        ("endInteractEvent", "string", 0x30), ("optionalInteractEvent", "string", 0x40)]),
    "GameObjectGfxStateTuning": (0x18, [("name", "string", 0xC)]),
    "CombatantDef": (0x10, [("deathEvent", "string", 0xC)]),
    "TriggerVolumeComponentDef": (0x4, [("triggerVolume", "struct", 0, "TriggerVolumeDef")]),
    "SpaceshipSpawnPointDef": (0x4, []),
    "NonPlayerClass": (0x7C, [
        ("name.text", "string", 0x10), ("name.id", "string", 0x14), ("mpClassAttributes", "string", 0xC),
        ("mpClassEffect", "string", 0x8), ("description.text", "string", 0x50), ("description.id", "string", 0x54),
        ("dropType", "array", 0x28, 4), ("longDescription", "array", 0x68, "cLongDescription"),
        ("eliteAffix", "array", 0x70, "cEliteAffix")]),
    "cLongDescription": (0x14, [("description.text", "string", 0), ("description.id", "string", 4)]),
    "cEliteAffix": (0xC, [("mpNPCAffix", "string", 0)]),
}


def payloads(package, kinds):
    """{type: {instance: payload}} for the given types in a DBPF 3 package."""
    with open(package, "rb") as handle:
        data = handle.read()
    if data[:4] != b"DBPF":
        raise ValueError("not a DBPF package")
    count = struct.unpack_from("<I", data, 0x24)[0]
    position = struct.unpack_from("<Q", data, 0x40)[0]
    flags = struct.unpack_from("<I", data, position)[0]
    position += 4
    shared = {}
    for bit, name in ((1, "type"), (2, "group"), (4, "high")):
        if flags & bit:
            shared[name] = struct.unpack_from("<I", data, position)[0]
            position += 4
    found = {kind: {} for kind in kinds}
    for _ in range(count):
        kind = shared.get("type")
        if kind is None:
            kind = struct.unpack_from("<I", data, position)[0]
            position += 4
        if "group" not in shared:
            position += 4
        if "high" not in shared:
            position += 4
        instance, offset, stored, size, compression, _flags = struct.unpack_from("<IIIIHH", data, position)
        position += 20
        if kind in found:
            raw = data[offset:offset + (stored & 0x7FFFFFFF)]
            found[kind][instance] = refpack(raw, size) if compression == 0xFFFF else raw
    return found


def walk(payload, layout):
    """{pointer offset: what it points to} for every set pointer in an asset: the string, or the
    offset of the struct image, or (offset, count) of the array. The asset must be used up exactly."""
    cursor = Cursor(payload)
    found = {}

    def visit(name, base):
        for field in LAYOUT[name][1]:
            kind, at = field[1], base + field[2]
            if kind == "inline":
                visit(field[3], at)
            elif struct.unpack_from("<I", payload, at)[0] == 0:
                continue
            elif kind == "string":
                found[at] = cursor.reference(at)
            elif kind == "struct":
                found[at] = cursor.reserve(1, LAYOUT[field[3]][0])
                visit(field[3], found[at])
            else:
                count = struct.unpack_from("<I", payload, at + 4)[0]
                element = field[3]
                stride = element if isinstance(element, int) else LAYOUT[element][0]
                found[at] = (cursor.reserve(count, stride), count)
                if not isinstance(element, int):
                    for index in range(count):
                        visit(element, found[at][0] + index * stride)

    cursor.reserve(1, LAYOUT[layout][0])
    visit(layout, 0)
    if cursor.offset != len(payload):
        raise ValueError(f"{len(payload) - cursor.offset} bytes left over")
    return found


# An XML element is (tag, value): value is the text, None for an empty element, a list of child
# elements, or UNSUPPORTED for one this script cannot write. MISSING stands for an element that
# recap_parser leaves out.
UNSUPPORTED = object()
MISSING = object()


def u32(payload, offset):
    return struct.unpack_from("<I", payload, offset)[0]


def number(payload, offset):
    return f"{struct.unpack_from('<f', payload, offset)[0]:.5f}"


def flag(payload, offset):
    return "true" if payload[offset] else "false"


def vector(payload, offset):
    return [(axis, number(payload, offset + 4 * index)) for index, axis in enumerate("xyz")]


def text(found, offset):
    """A pointer's string, or None when the pointer is not set (recap_parser then leaves the element out)."""
    value = found.get(offset)
    return value if isinstance(value, str) else None


THUMBNAIL = [
    ("fovY", "number", 0x0), ("nearPlane", "number", 0x4), ("farPlane", "number", 0x8),
    ("cameraPosition", "vector", 0xC), ("cameraScale", "number", 0x18), ("cameraRotation_0", "vector", 0x1C),
    ("cameraRotation_1", "vector", 0x28), ("cameraRotation_2", "vector", 0x34),
    ("mouseCameraDataValid", "flag", 0x40), ("mouseCameraOffset", "vector", 0x44),
    ("mouseCameraSubjectPosition", "vector", 0x50), ("mouseCameraTheta", "number", 0x5C),
    ("mouseCameraPhi", "number", 0x60), ("mouseCameraRoll", "number", 0x64), ("poseAnimID", "uint", 0x68)]


def fields(payload, found, base, spec):
    """Elements for (tag, kind, offset) fields of the struct at base; a string field whose pointer
    is not set is left out."""
    out = []
    for tag, kind, offset in spec:
        at = base + offset
        if kind == "number":
            out.append((tag, number(payload, at)))
        elif kind == "vector":
            out.append((tag, vector(payload, at)))
        elif kind == "flag":
            out.append((tag, flag(payload, at)))
        elif kind == "uint":
            out.append((tag, str(u32(payload, at))))
        elif text(found, at) is not None:
            out.append((tag, escape(text(found, at))))
    return out


def noun_prefix(payload, found):
    """The noun's XML before eliteAssetIds, as recap_parser writes it. The gfx, door and switch
    structs are UNSUPPORTED: only checked for being where they are, never written."""
    out = fields(payload, found, 0, [
        ("nounType", "uint", 0x0), ("clientOnly", "flag", 0x4), ("isFixed", "flag", 0x5),
        ("isSelfPowered", "flag", 0x6), ("lifetime", "number", 0xC), ("gfxPickMethod", "uint", 0x8),
        ("graphicsScale", "number", 0x14), ("modelKey", "string", 0x24), ("prefab", "string", 0x10),
        ("levelEditorModelKey", "string", 0x34)])
    out.append(("cSPBoundingBox", [("min", vector(payload, 0x38)), ("max", vector(payload, 0x44))]))
    out += fields(payload, found, 0, [
        ("presetExtents", "uint", 0x50), ("voice", "string", 0x60), ("foot", "string", 0x70),
        ("flightSound", "string", 0x80)])
    for tag, offset in (("gfxStates", 0x84), ("doorDef", 0x88), ("switchDef", 0x8C), ("pressureSwitchDef", 0x90)):
        if offset in found:
            out.append((tag, UNSUPPORTED))
    if 0x94 in found:
        # Offsets from AssetData.Parser. They fit the names: rarity is 0, 1, 2 for plain, _rare
        # and _epic crystals, and type is the same for all three.
        out.append(("crystalDef", fields(payload, found, found[0x94], [
            ("modifier", "string", 0xC), ("type", "uint", 0x10), ("rarity", "uint", 0x14)])))
    out.append(("assetId", f"0x{struct.unpack_from('<Q', payload, 0x98)[0]:X}"))
    out += fields(payload, found, 0, [
        ("npcClassData", "string", 0xA0), ("playerClassData", "string", 0xA4),
        ("characterAnimationData", "string", 0xA8)])
    if 0xAC in found:
        out.append(("creatureThumbnailData", fields(payload, found, found[0xAC], THUMBNAIL)))
    return out


def noun_tail(payload, found):
    """The noun's XML from eliteAssetIds on, in recap_parser's order and format."""
    out = []
    start, count = found.get(0xB0, (0, 0))
    if count:
        # Decimal, not hex like assetId: Noun::Read parses list entries as base-10 numbers.
        ids = [("entry", str(struct.unpack_from("<Q", payload, start + 8 * index)[0])) for index in range(count)]
        out.append(("eliteAssetIds", ids))
    elif 0xAC in found:
        # recap_parser writes the empty list only after a thumbnail.
        out.append(("eliteAssetIds", None))
    out += fields(payload, found, 0, [
        ("physicsType", "uint", 0xB8), ("density", "number", 0xBC), ("physicsKey", "string", 0xCC),
        ("affectsNavMesh", "flag", 0xD0), ("dynamicWall", "flag", 0xD1), ("hasLocomotion", "flag", 0xDB),
        ("locomotionType", "uint", 0xDC), ("hasNetworkComponent", "flag", 0xD8),
        ("hasCombatantComponent", "flag", 0xDA), ("aiDefinition", "string", 0xD4),
        # The client's flag, not recap_parser's (see NOUN_MISREADS).
        ("hasCameraComponent", "flag", 0xD9), ("spawnTeamId", "uint", 0xE0), ("isIslandMarker", "flag", 0xE4)])
    for tag, offset in (
            ("activateFnNamespace", 0xE8), ("tickFnNamespace", 0xEC), ("deactivateFnNamespace", 0xF0),
            ("startFnNamespace", 0xF4), ("endFnNamespace", 0xF8), ("triggerVolume", 0x124), ("projectile", 0x128)):
        if offset in found:
            out.append((tag, UNSUPPORTED))
    if 0x12C in found:
        out.append(("orbit", fields(payload, found, found[0x12C], [
            ("orbitHeight", "number", 0), ("orbitRadius", "number", 4), ("orbitSpeed", "number", 8)])))
    if 0x130 in found:
        out.append(("locomotionTuning", fields(payload, found, found[0x130], [
            ("acceleration", "number", 0), ("deceleration", "number", 4), ("turnRate", "number", 8)])))
    out += fields(payload, found, 0, [("gravityData", "string", 0x134)])
    components = any(u32(payload, 0xFC + offset) for offset in range(0, 0x28, 4))
    out.append(("SharedComponentData", UNSUPPORTED if components else None))
    out += fields(payload, found, 0, [
        ("isFlora", "flag", 0x148), ("isMineral", "flag", 0x149), ("isCreature", "flag", 0x14A),
        ("isPlayer", "flag", 0x14B), ("isSpawned", "flag", 0x14C), ("toonType", "string", 0x144),
        ("modelEffect", "string", 0x15C), ("removalEffect", "string", 0x16C),
        ("meleeDeathEffect", "string", 0x18C), ("meleeCritEffect", "string", 0x19C),
        ("energyDeathEffect", "string", 0x1AC), ("energyCritEffect", "string", 0x1BC),
        ("plasmaDeathEffect", "string", 0x1CC), ("plasmaCritEffect", "string", 0x1DC)])
    return out


def xml_text(elements, depth):
    lines = []
    for tag, value in elements:
        indent = "\t" * depth
        if value is None:
            lines.append(f"{indent}<{tag} />\n")
        elif isinstance(value, list):
            lines.append(f"{indent}<{tag}>\n")
            lines.append(xml_text(value, depth + 1))
            lines.append(f"{indent}</{tag}>\n")
        else:
            lines.append(f"{indent}<{tag}>{value}</{tag}>\n")
    return "".join(lines)


def same(node, value):
    """Whether a parsed element holds this value (text compared escaped, as it is written)."""
    children = list(node)
    if value is None:
        return not children and not (node.text or "").strip()
    if isinstance(value, list):
        return len(children) == len(value) and all(
            child.tag == tag and same(child, inner) for child, (tag, inner) in zip(children, value))
    return not children and escape(node.text or "") == value


class Check:
    """Mismatch counts per field, comparing decoded values with recap_parser's export.

    misreads names the fields recap_parser is known to get wrong, with why. For those a file may
    instead hold what recap_parser makes of the binary (passed to elements() as recap); that is
    counted apart and does not count as a mismatch."""

    def __init__(self, title, misreads=None):
        self.title = title
        self.misreads = misreads or {}
        self.compared = collections.Counter()
        self.wrong = collections.Counter()
        self.recap = collections.Counter()
        self.examples = collections.defaultdict(list)

    def element(self, field, file, node, value, recap):
        self.compared[field] += 1
        if node is not None and same(node, value):
            return
        if field in recap:
            misread = recap[field]
            if node is None if misread is MISSING else node is not None and same(node, misread):
                self.recap[field] += 1
                return
        found = "missing" if node is None else ElementTree.tostring(node, "unicode").strip()[:120]
        self.miss(field, file, found, value)

    def miss(self, field, file, found, expected):
        self.wrong[field] += 1
        if len(self.examples[field]) < 3:
            if isinstance(expected, list):
                expected = xml_text(expected, 0).replace("\n", "")
            self.examples[field].append(f"{file}: has {found!r}, binary {str(expected)[:120]!r}")

    def elements(self, file, nodes, values, recap=None):
        """Compares one file's elements with the decoded ones, by tag and by order."""
        recap = {field: value for field, value in (recap or {}).items() if field in self.misreads}
        by_tag = {}
        for node in nodes:
            by_tag.setdefault(node.tag, node)
        for tag, value in values:
            if value is not UNSUPPORTED:
                self.element(tag, file, by_tag.get(tag), value, recap)
        decoded = {tag for tag, _ in values}
        for node in nodes:
            if node.tag not in decoded:
                self.miss(node.tag, file, "present", "absent")
        self.compared["(order)"] += 1
        order = [tag for tag, _ in values if tag in by_tag]
        if [node.tag for node in nodes if node.tag in decoded] != order:
            self.miss("(order)", file, [node.tag for node in nodes][:8], order[:8])

    def report(self):
        print(f"{self.title}:")
        for field in sorted(self.compared, key=lambda name: (-self.wrong[name], -self.recap[name], name)):
            line = f"  {field:28} {self.compared[field]:5} compared, {self.wrong[field]} differ"
            if self.recap[field]:
                line += f", {self.recap[field]} as recap_parser misreads it ({self.misreads[field]})"
            print(line)
            for example in self.examples[field]:
                print(f"      {example}")
        return sum(self.wrong.values())


def instance_of(file, suffix):
    """The asset instance an XML file stands for: fnv of its name, or the name itself when
    recap_parser could only write the hash (0x1234ABCD.Noun.xml)."""
    name = file[:-len(suffix)]
    return int(name, 16) if name.lower().startswith("0x") else fnv(name)


def read_text(path):
    with open(path, "rb") as handle:
        return handle.read().decode("latin-1")


def write_text(path, content):
    # Whole or not at all: a run cut short must not leave a truncated file for the next one to choke on.
    temporary = path + ".tmp"
    with open(temporary, "wb") as handle:
        handle.write(content.encode("latin-1"))
        handle.flush()
        os.fsync(handle.fileno())
    os.replace(temporary, path)


def children(content, root):
    """Splits recap_parser XML into its root's child elements, each on lines of its own at one tab:
    (head, [(tag, text)], foot). Returns None if the file is not laid out that way. Lines end at
    "\n" only, as a misread string can hold a "\r"."""
    lines = re.findall(r"[^\n]*\n|[^\n]+$", content)
    if len(lines) < 3 or lines[1] != f"<{root}>\n" or lines[-1] != f"</{root}>\n":
        return None
    out = []
    index = 2
    while index < len(lines) - 1:
        match = re.match(r"\t<([A-Za-z_][\w.]*)( />|>(.*</\1>)?)\n$", lines[index])
        if not match:
            return None
        end = index
        if match.group(2) == ">":
            closing = f"\t</{match.group(1)}>\n"
            while end < len(lines) - 1 and lines[end] != closing:
                end += 1
            if end == len(lines) - 1:
                return None
        out.append((match.group(1), "".join(lines[index:end + 1])))
        index = end + 1
    return lines[0] + lines[1], out, lines[-1]


NOUN_MISREADS = {
    "hasCameraComponent": "true when the aiDefinition pointer at +0xD4 is set; "
        "the client's flag at +0xD9 is 0 in every noun",
    "crystalDef": "type and rarity read 8 bytes early, from +0x8 and +0x10",
}


def noun_recap(payload, found):
    """What recap_parser writes for the fields in NOUN_MISREADS."""
    recap = {"hasCameraComponent": "true" if u32(payload, 0xD4) else "false"}
    if 0x94 in found:
        recap["crystalDef"] = fields(payload, found, found[0x94], [
            ("modifier", "string", 0xC), ("type", "uint", 0x8), ("rarity", "uint", 0x10)])
    return recap


def fix_nouns(folder, assets, dry_run):
    check = Check("Nouns: decoded fields against the nouns without eliteAssetIds", NOUN_MISREADS)
    broken = []
    missing = 0
    for file in sorted(os.listdir(folder)):
        if not file.endswith(".Noun.xml"):
            continue
        instance = instance_of(file, ".Noun.xml")
        payload = assets.get(instance)
        if payload is None:
            missing += 1
            continue
        found = walk(payload, "Noun")
        if found.get(0xB0, (0, 0))[1]:
            broken.append((file, payload, found))
            continue
        nodes = list(ElementTree.parse(os.path.join(folder, file)).getroot())
        check.elements(file, nodes, noun_prefix(payload, found) + noun_tail(payload, found), noun_recap(payload, found))
    if check.report():
        print("nouns: the decoder disagrees with the exported nouns, so none were written", file=sys.stderr)
        return False

    prefix_check = Check("Nouns with eliteAssetIds: the kept part before it, against the binary", NOUN_MISREADS)
    changed = []
    for file, payload, found in broken:
        path = os.path.join(folder, file)
        content = read_text(path)
        # Everything from the first misread field on is replaced, so only the part before it is parsed.
        cut = re.search(r"^\t<(eliteAssetIds|physicsType)[ >]", content, re.M)
        split = children(content[:cut.start()] + "</noun>\n", "noun") if cut else None
        tail = noun_tail(payload, found)
        if split is None or any(value is UNSUPPORTED for _, value in tail):
            print(f"{file}: cannot rewrite (unexpected layout)", file=sys.stderr)
            return False
        head, parts, foot = split
        kept = "".join(block for _, block in parts)
        nodes = list(ElementTree.fromstring(f"<noun>\n{kept}</noun>".encode("latin-1")))
        prefix_check.elements(file, nodes, noun_prefix(payload, found), noun_recap(payload, found))
        repaired = head + kept + xml_text(tail, 1) + foot
        if repaired != content:
            changed.append((path, repaired))
    if prefix_check.report():
        print("nouns: the kept part of a damaged noun disagrees with the binary, so none were written", file=sys.stderr)
        return False
    for path, repaired in changed:
        if not dry_run:
            write_text(path, repaired)
        print(f"  {'would rewrite' if dry_run else 'rewrote'} noun/{os.path.basename(path)}")
    print(f"nouns: {len(broken)} with eliteAssetIds, {len(changed)} {'to change' if dry_run else 'changed'}, "
        f"{len(broken) - len(changed)} already right, {missing} XML files without binary")
    return True


def localized(found, base):
    """recap_parser's <text>/<id> pair for a cLocalizedAssetString, the bare text when it has no
    id, or None when it has no text."""
    if base not in found:
        return None
    if base + 4 not in found:
        return escape(found[base])
    return [("text", escape(found[base])), ("id", escape(found[base + 4]))]


def npc_elements(payload, found):
    """The class's XML in recap_parser's order, with challengeValue, npcRank and dropType as the binary has them."""
    out = [("testingOnly", flag(payload, 0x0))]
    if localized(found, 0x10):
        out.append(("name", localized(found, 0x10)))
    out += [
        ("creatureType", str(u32(payload, 0x4))), ("aggroRange", number(payload, 0x38)),
        ("alertRange", number(payload, 0x3C)), ("dropAggroRange", number(payload, 0x40)),
        ("challengeValue", str(struct.unpack_from("<i", payload, 0x24)[0])),
        ("mNPCType", str(u32(payload, 0x44))), ("npcRank", str(struct.unpack_from("<i", payload, 0x48)[0]))]
    out += fields(payload, found, 0, [("mpClassAttributes", "string", 0xC), ("mpClassEffect", "string", 0x8)])
    if localized(found, 0x50):
        out.append(("description", localized(found, 0x50)))
    start, count = found.get(0x28, (0, 0))
    if count:
        out.append(("dropType", [("entry", str(u32(payload, start + 4 * index))) for index in range(count)]))
    out += [
        ("dropDelay", number(payload, 0x34)), ("targetable", flag(payload, 0x4C)),
        ("playerCountHealthScale", number(payload, 0x64))]
    start, count = found.get(0x68, (0, 0))
    if count:
        out.append(("cLongDescription", [
            ("entry", [("description", localized(found, start + 0x14 * index))]) for index in range(count)]))
    start, count = found.get(0x70, (0, 0))
    if count:
        out.append(("eliteAffix", [("entry", fields(payload, found, start + 0xC * index, [
            ("mpNPCAffix", "string", 0), ("minDifficulty", "uint", 4), ("maxDifficulty", "uint", 8)]))
            for index in range(count)]))
    out.append(("playerPet", flag(payload, 0x78)))
    return out


# Fields the class XML gets from the binary even when recap_parser wrote them; the rest is kept.
NPC_WRITTEN = ("challengeValue", "npcRank", "dropType", "eliteAffix")


NPC_MISREADS = {
    "challengeValue": "left out",
    "npcRank": "a copy of mNPCType; the rank is at +0x48",
    "dropType": "each entry nested in another <entry>, which NonPlayerClass::Read reads as 0",
}


def fix_nonplayerclasses(folder, assets, dry_run):
    check = Check("NonPlayerClass: decoded fields against recap_parser's export", NPC_MISREADS)
    plans = []
    missing = 0
    for file in sorted(os.listdir(folder)):
        if not file.endswith(".NonPlayerClass.xml"):
            continue
        instance = instance_of(file, ".NonPlayerClass.xml")
        payload = assets.get(instance)
        if payload is None:
            missing += 1
            continue
        path = os.path.join(folder, file)
        content = read_text(path)
        values = npc_elements(payload, walk(payload, "NonPlayerClass"))
        recap = {"challengeValue": MISSING, "npcRank": str(u32(payload, 0x44))}
        for tag, value in values:
            if tag == "dropType":
                recap[tag] = [("entry", [entry]) for entry in value]
        check.elements(file, list(ElementTree.fromstring(content.encode("latin-1"))), values, recap)
        plans.append((path, content, values))
    if check.report():
        print("nonplayerclass: the decoder disagrees with the export, so nothing was written", file=sys.stderr)
        return False

    changed = []
    added = collections.Counter()
    for path, content, values in plans:
        split = children(content, "nonplayerclass")
        if split is None:
            print(f"{os.path.basename(path)}: cannot rewrite (unexpected layout)", file=sys.stderr)
            return False
        # The check above found every element of the file among the decoded ones, so rebuilding
        # the file in the decoded order keeps them all.
        head, parts, foot = split
        kept = dict(parts)
        blocks = []
        for tag, value in values:
            if tag in NPC_WRITTEN or tag not in kept:
                blocks.append(xml_text([(tag, value)], 1))
                if tag not in kept:
                    added[tag] += 1
            else:
                blocks.append(kept[tag])
        repaired = head + "".join(blocks) + foot
        if repaired != content:
            changed.append((path, repaired))
    for path, repaired in changed:
        if not dry_run:
            write_text(path, repaired)
    print(f"nonplayerclass: {len(plans)} classes, {len(changed)} {'to change' if dry_run else 'changed'}, "
        f"{missing} without binary; elements added: {dict(added) or 'none'}")
    return True


def markerset_group(payload):
    """(group, weight) of a binary Markerset. The group string ends the asset (before any
    conditions); it is found by the hash its Key holds, as other data can run into it with no NUL."""
    if u32(payload, 0x14) == 0:
        return "", struct.unpack_from("<f", payload, 0x18)[0]
    end = len(payload) - (4 * u32(payload, 0x24) if u32(payload, 0x20) else 0) - 1
    if payload[end] != 0:
        raise ValueError("no group string at the end")
    for start in range(end - 1, -1, -1):
        if payload[start] == 0:
            break
        group = payload[start:end].decode("latin-1")
        if fnv(group) == u32(payload, 0x08):
            return group, struct.unpack_from("<f", payload, 0x18)[0]
    raise ValueError("group string does not match its hash")


def level_markersets(payload):
    """The markerset asset names a binary Level lists, in order."""
    cursor = Cursor(payload)
    cursor.reserve(1, 0x98)
    start = cursor.reserve(u32(payload, 4), 4) if u32(payload, 0) else 0
    names = [cursor.reference(start + 4 * index) for index in range(u32(payload, 4) if start else 0)]
    return [name for name in names if name is not None]


def fix_levels(data, markersets, levels, dry_run):
    check = Check("Markerset: decoded group and weight against markerset/*.Markerset.xml")
    pattern = re.compile(r"<group>(.*)</group>\n\t<weight>(.*)</weight>\n</markerset>\n$")
    groups = {}
    for instance, payload in markersets.items():
        groups[instance] = markerset_group(payload)
    folder = os.path.join(data, "markerset")
    for file in sorted(os.listdir(folder)):
        if not file.endswith(".Markerset.xml"):
            continue
        instance = instance_of(file, ".Markerset.xml")
        if instance not in groups:
            continue
        with open(os.path.join(folder, file), "rb") as handle:
            handle.seek(max(0, os.path.getsize(handle.name) - 512))
            match = pattern.search(handle.read().decode("latin-1"))
        group, weight = groups[instance]
        for field, found, wanted in (("group", match and match.group(1), escape(group)),
                ("weight", match and match.group(2), f"{weight:.5f}")):
            check.compared[field] += 1
            if found != wanted:
                check.miss(field, file, found, wanted)
    if check.report():
        print("level: the markerset decoder disagrees with the export, so no level was written", file=sys.stderr)
        return False
    odd = [weight for _, weight in groups.values() if weight != int(weight) or weight < 0]
    if odd:
        print(f"level: weights that are not whole numbers: {odd[:5]}", file=sys.stderr)
        return False

    folder = os.path.join(data, "level")
    changed = entries = missing = unknown = 0
    for file in sorted(os.listdir(folder)):
        if not file.endswith(".Level.xml"):
            continue
        instance = instance_of(file, ".Level.xml")
        path = os.path.join(folder, file)
        content = read_text(path)
        listed = re.findall(r"^\t\t\t<markersetAsset>(.*)</markersetAsset>$", content, re.M)
        if instance not in levels or level_markersets(levels[instance]) != listed:
            print(f"level/{file}: its markerset list does not match the binary level, left alone", file=sys.stderr)
            missing += 1
            continue
        lines = content.splitlines(keepends=True)
        out = []
        absent = []
        inside = False
        for line in lines:
            if line == "\t<markersets>\n":
                inside = True
            elif line == "\t</markersets>\n":
                inside = False
            elif inside and re.match(r"\t\t\t<(group|weight)>", line):
                continue
            out.append(line)
            match = re.match(r"\t\t\t<markersetAsset>(.*)\.Markerset</markersetAsset>\n$", line) if inside else None
            if match:
                # A set the package does not have cannot load; it gets no group and weight 0.
                group, weight = groups.get(fnv(match.group(1)), ("", 0.0))
                if fnv(match.group(1)) not in groups:
                    absent.append(match.group(1))
                out.append(f"\t\t\t<group>{escape(group)}</group>\n")
                out.append(f"\t\t\t<weight>{int(weight)}</weight>\n")
                entries += 1
        if absent:
            print(f"  level/{file}: not in the package, so group '' and weight 0: {', '.join(absent)}")
            unknown += len(absent)
        repaired = "".join(out)
        if repaired != content:
            changed += 1
            if not dry_run:
                write_text(path, repaired)
    print(f"level: {entries} markerset entries ({unknown} naming a set the package lacks), "
        f"{changed} level files {'to change' if dry_run else 'changed'}, "
        f"{missing} left alone")
    return not missing


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--game", default=DEFAULT_GAME, help="the Darkspore install folder")
    parser.add_argument("--package", help="default: <game>/Data/AssetData_Binary.package")
    parser.add_argument("--data", help="default: <game>/DarksporeBin/Server/data/serverdata")
    parser.add_argument("--dry-run", action="store_true", help="report what would change and write nothing")
    arguments = parser.parse_args()
    arguments.package = arguments.package or os.path.join(arguments.game, "Data", "AssetData_Binary.package")
    arguments.data = arguments.data or os.path.join(arguments.game, "DarksporeBin", "Server", "data", "serverdata")

    assets = payloads(arguments.package, (NOUN_TYPE, NONPLAYERCLASS_TYPE, MARKERSET_TYPE, LEVEL_TYPE))
    results = [
        fix_nouns(os.path.join(arguments.data, "noun"), assets[NOUN_TYPE], arguments.dry_run),
        fix_nonplayerclasses(os.path.join(arguments.data, "nonplayerclass"), assets[NONPLAYERCLASS_TYPE], arguments.dry_run),
        fix_levels(arguments.data, assets[MARKERSET_TYPE], assets[LEVEL_TYPE], arguments.dry_run),
    ]
    if arguments.dry_run:
        print("dry run: nothing was written")
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
