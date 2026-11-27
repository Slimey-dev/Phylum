# AI director

The director decides which enemies a level uses, where in the level each kind shows up, and how big
each group is. The code is `director.h` (the rules) and `level.h` (the marker and markerset data
they read). Every function is deterministic given a `DirectorRandom`, so tests script the rolls with
`ScriptedDirectorRandom`; the server uses `DefaultDirectorRandom` (`utils::random`).

Nothing calls the director yet. The pieces below are built and tested; wiring them into a level is
the next step.

## Where the rules come from

- **Darkspore.exe 5.3.0.103.** The client still has the director's decision code (class
  `cAIDirector`, 0x4d0 bytes, at simulator + 0x558): sorting markers, building the level's enemy
  pool, splitting it over sections and building groups against a budget. It does not have the
  scheduler that decides *when* a group spawns. That ran on the server: the client calls
  `Director::Init(level, populate = false)` (0x4e74b6), its horde and boss triggers (0x9facb0,
  0x9facf0) return without doing anything, and Lua's `ActivateHordeSpawn` (0xa23560) is a stub.
- **Daniel Kline, "The AI Director of Darkspore", GDC 2012.** The director's designer gives the
  wander point chances and the spike table. These are the only source for the scheduler, so
  everything built from them is approximate.
- **darkspin** ([github.com/darkspinnet/darkspin](https://github.com/darkspinnet/darkspin), MIT),
  a Go server, for fallback spike budgets. Its authors label these as local tuning, not retail
  values.

The difficulty used everywhere is the chain level, 1 to 72 (1 on a first run of 1-1). The tier of
a difficulty is ceil(difficulty / 4).

## Ported from the client

| Code | Client | What it does |
| --- | --- | --- |
| `ClassifySpawnMarker` | 0x9fa9b0 | Director kind of a marker's noun |
| `ChooseLevelPool` | 0x9fbd10, 0x9fb530 | The level's 3 minion and 3 special types on a first visit |
| `DefaultSectionBuckets` | SectionConfig asset | How many types each section gets |
| `AllocateSections` | end of 0x9fc430 | Splits the level's types over sections A, B and C |
| `DirectorGroup::Add` | 0x9fba70 | Adds one type to a group if it fits the budget |
| `DirectorAgentAllowed` | 0x9faa40 | Whether the player count leaves room for an agent |
| `BuildMixedGroup` | 0x9fd660 (0x9fe270 type 4) | A group of minions, specials and agents |
| `PickWanderer` | 0x9fe270 type 1 | One minion of a section |

**Marker kinds.** The client compares the noun id with fixed hashes: SpawnPoint_Director 1,
DirectorA 2, DirectorB 3, DirectorHorde 5, HordeTrigger 6, DirectorWanderer 7, DirectorSpike 8,
DirectorBoss 9. Everything else is 0, including SpawnPoint_DirectorC, whose hash appears nowhere in
the exe. The client also maps the id 4 to 1, which no known noun hashes to; that is ported as is.

**Level pool.** On a first visit (any player whose progress, player + 0x125c, is below the
difficulty) the client keeps the firstTimeConfig entries whose difficulty range holds the
difficulty. A list longer than 3 is shuffled (element i swaps with a random one at or before it)
and cut to 3, minions first, then specials. This pool is section 3, and is the same 3 + 3 list
Phylum sends in ChainVoteData.

**Sections.** SectionConfig is 72 buckets of {numMinions, numSpecials, difficulty, chance}. In
`Data/AssetData_Binary.package` (type 0xa38fa119, instance 0x45b7b07c) every tier 1 to 18 has four
buckets, (2, 1), (2, 2), (2, 1), (2, 2), with chance 0. The exported SectionConfig XML is misparsed,
so `DefaultSectionBuckets` writes the buckets out. For sections A, B and C in turn the client:

1. picks a random bucket of the tier that no earlier section took (it needs more than two buckets
   of the tier, or A, B and C stay empty);
2. for section C only, turns a bucket without specials into one minion fewer and one special;
3. draws that many minion and special types from the level pool with a bag. The bags are shared
   by the three sections and refill when they run dry, so a section can get one type twice, which
   makes that type likelier in its groups.

**Group cost.** A random candidate costs (1 + 0.2 × members) × (group challenge + its challenge),
worked out in single precision as the client does (0x9fbaff-0x9fbb26). If that is at most the
budget it joins the group; otherwise that type leaves the candidate list for this group. Groups stop
at 15 members.

**Mixed group.** First a roll caps specials and agents together: under 0.05 no cap, under 0.20 two,
under 0.70 one, otherwise none (and the section's specials are emptied). Then, until the group has
15 members, its last cost reaches the budget or the minion list it used last is empty:

- the client rolls a number and never uses it (0x9fdad2), which is kept so the rolls line up;
- minions come from the group's section while it has fewer than 4, then from the next section
  (A to B, B to C, C to A) and its own together while fewer than 8, then from the whole level;
- specials come from the section for the first 2, then from the whole level;
- a minion is added when there are no specials to take or the cap is reached. Otherwise, if
  agents exist and are allowed, a roll under 0.1 adds an agent (only from difficulty 7); anything
  else adds a special.

Agents are the planetConfig agents of the difficulty. `DirectorAgentAllowed` is the client's gate:
difficulty 7 or more, and fewer agents than one for two players, or the players minus two for more
(none for one player).

The tuning is the client's prop file (group and instance 0x3b01d7f6 in ServerData.package), read in
0x9cf190. The file's values replace the exe's compiled-in defaults:

| Value | Address | Prop |
| --- | --- | --- |
| Cost step 0.2 | 0x14cb084 | 2ccf993d |
| Specials cap rolls 0.05, 0.15, 0.5 | 0x14cb070, 0x14cb06c, 0x14cb068 | a547e347, 5b780357, 61735c75 |
| Agent chance 0.1 | 0x14cb074 | df7e68d1 |
| Specials from the section 2 | 0x14cb078 | 2edb3805 |
| Minions from the section 4, with the next section 8 | 0x14cb07c, 0x14cb080 | e0da6cd6, 9931d732 |
| Agents from difficulty 7 | 0x1164ba4 | dbae1599 |
| Start with a special: off | 0x14cafe7 | 65648990 |

### Where Phylum differs

- `ChooseLevelPool` keeps each noun once. The client does not, but no shipped firstTimeConfig lists
  a noun twice for one difficulty, so the result is the same.
- Only the first-visit pool is ported. The repeat-visit pool (0x9fc150: levelConfig, planetConfig
  and the element configs of 0x9fa7f0) is not.
- The client filters every list by difficulty again when it builds a group. `DirectorEnemy` keeps no
  range, and the pool is built for one difficulty, so that step is left out.
- `PickWanderer` takes no budget (the client checks one through AddToGroup, which every type fits
  without one). When the section has no minions it falls back to the level's minions, then the
  section's specials, then the level's; the client spawns nothing.
- A section number above 3 counts as 3.

## Approximate

These have no client code behind them. Each constant is named in `director.cpp`.

- **Wander points (`RollWanderHit`).** From the talk: a wander point spawns on 18% of first hits and
  25% of hits 2 to 5, and never after the 5th. A spawn is 2, 3 or 4 wanderers instead of one 12, 6
  and 2% of the time. The talk does not say whether hits count per point or per run (darkspin counts
  them per run and starts again after a spawn), so the caller passes the hit number.
- **Spikes (`NextSpikeIntensity`, `SpikeHistory`).** The talk's table picks the next spike from the
  last two and whether the players are doing well:

  | Last two | Doing well | Doing poorly |
  | --- | --- | --- |
  | Hard, Medium | Hard | Medium |
  | Hard, Skip | Hard | Medium |
  | Medium, Hard | Medium | Skip |
  | Medium, Medium | Hard | Skip |
  | Medium, Skip | Hard | Medium |
  | Skip, Hard | Medium | Skip |
  | Skip, Medium | Hard | Medium |

  It never gives two Hard or two Skip spikes in a row, so those pairs act as the opening, which is
  (Skip, Medium). What "doing well" means is the caller's.
- **Spike budgets (`SpikeBudget`).** Hard is 50, 80 and 110 for sections A, B and C (darkspin's
  fallback) and 80 for "any" (darkspin uses B's). Medium is half of Hard and Skip is nothing. The
  difficulty is taken but not used yet: nothing says how budgets grow with the chain level. The
  talk's spike challenge table (n enemies with L of them lieutenants cost (n + 4) × (n + 5L), which
  fits all 128 cells) and the director ints the client loads but never reads (55, 80, 80, 80, 230,
  60, 9 and 250 at 0x14cb000-0x14cb01c, same prop file) are candidates for that. What those ints
  mean is a guess.

## How the server runs it

- **The pool.** `Instance::LoadLevel` takes the level's firstTimeConfig on a first visit
  (levelConfig when it gives no minions, or on a repeat visit) through `ChooseLevelPool` and
  `AllocateSections`, with the chain level as the difficulty. The level pool's three minions and three
  specials also go to the chain vote's six enemy slots, which the client preloads; an empty slot is 0,
  not the hash of an empty name, since the client's preload does not check that a noun exists.
- **The points.** `Instance::BuildDirectorSpawner` runs once per game. It takes every markerset of
  group "none" and one of each other group by weight, as 0x9d6340 does but with the server's own roll
  (the client's seed is not followed yet). Wanderer markers become wander points; the Spike markers of
  one markerset become one spike set, in the section most of them name.
- **When they fire (`DirectorSpawner`, approximate).** Four times a second, `Instance::UpdateDirector`
  passes the living heroes' positions. A wander point is hit when a hero comes within 25 m of it, but
  not if a hero was already that close when the level started, so nothing spawns on the players. A hit
  rolls `RollWanderHit`; a point that spawns, or has had five hits, is done. A spike set fires once,
  the first time a hero comes within 25 m of one of its points: `SpikeHistory` picks Hard, Medium or
  Skip, `SpikeBudget` gives the budget and `BuildMixedGroup` the group, spread over the set's points.
  "Doing well" is the heroes having half their health or more. Creatures are set within 2 m of their
  point: the client scatters by up to 10 m and projects onto the navmesh, which the server has not got.
- **Spawning.** Through `Instance::SpawnCreature`, so a creature reaches the client on the next tick.
  `DIRECTOR_SPAWNS` in config.xml (true by default) turns the director's spawns off.

## Level data

- `Marker::GetSpawnSection` and `Marker::ActivatesSpike` read the marker's
  `<SharedComponentData><spawnPointDef>` (`sectionType` and `activatesSpike`). Either can be
  missing; then they are 3 (any) and true, the defaults of the SpawnPoint_Director* nouns. Every
  director marker in the shipped data sets activatesSpike.
- `Level::GetMarkersetEntries` lists every markerset the level names, in order, with its group and
  weight. An entry's own `<group>` and `<weight>` come first, then the ones after the markers in the
  markerset file, then "" and 1. The client loads every markerset of group "none" and one of each
  other group, chosen by weight (0x9d6340). The level still loads every markerset it lists.
- `LevelConfig::GetMinions`, `GetSpecials` and `GetAgents` give the lists the director takes as
  `DirectorClassEntry`.

## Data it depends on

`scripts/serverdata_fix.py` repairs all of the following from Data/AssetData_Binary.package, and
`scripts/linux/install.sh` runs it. Run it again after the installer re-exports the data.

- **challengeValue.** The exported NonPlayerClass XML leaves out challengeValue (NonPlayerClass +
  0x24). Until it is repaired every challenge is 0, every type costs nothing, and every group with
  a budget above 0 fills up to 15. The raw values for 1-1 include ZelemBasicRanged 6,
  ZelemBasicRepair 4, ZelemSpecialHaster 30, NomadSnipe 27 and NomadWithDrone 30.
- **Noun tails.** Nouns with eliteAssetIds are misparsed from that field on, losing
  hasNetworkComponent, isCreature and aiDefinition. That does not change the pool, but those nouns
  will not spawn properly until it is repaired.
- **Markerset groups.** Choosing markersets by group needs the group and weight of each level
  entry, from the data repair or the markerset files.
- **SectionConfig.** Misparsed in the export, which is why `DefaultSectionBuckets` exists.

## Not built yet

- **The client's own scheduler parts:** its spawn records (0x9fb420), its seeded markerset choice
  (0x9d6340, so the server and client pick the same variants), and dropping spike sets inside a horde
  or boss trigger (0x9fd490).
- **Tier 2 and 3 enemies:** their nouns are filed under their hashes (0xXXXXXXXX.Noun.xml), so the
  noun database does not find them by name, and a level past difficulty 24 has no minions.
- **The repeat-visit pool** (0x9fc150), which mixes in the planet and element configs.
- **Hordes:** the horde group (0x9fdd50), waves (DirectorTuning HordeDifficultyWave, 4 at difficulty
  1), HordeTrigger and survivor hordes.
- **The boss** (0x9fe270 type 8, 0x9fd260) and DirectorTrigger_SpawnBoss.
- **Captains and elites:** SpawnCreature (0x9fe7d0) with its elite rolls (EliteNPCGlobals), the
  captain and elite affixes, and nGameObject.MakeElite, a stub in the client.
- The other group types of 0x9fe270 (one special, minions only, level specials, one agent,
  victims) and the spawn scatter (0x9fb180).
- **DirectorState** (message 0x8B), which drives the horde and boss HUD.
- **Pre-aggro hiding:** enemies whose AI starts in nBehavior_Invisible stay hidden until first aggro.
- **Orbs** dropped on death (0x9ce140, 0x9fb360).
- **Difficulty scaling** of enemy health and damage (0x9e2d20) and of spike budgets.
