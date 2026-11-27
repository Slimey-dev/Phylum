# Solo campaign plan

Goal: play the Darkspore campaign alone from start to finish, with working combat, enemies, bosses, loot and progression.

The result should play recognisably like the original. It cannot be exact, because EA's server code is gone. Where the game's files hold a rule, we use it. Where they do not, we reconstruct it from the client and from videos, and mark it as approximate.

Multiplayer, PvP and public hosting are out of scope.

## How we work

- Run the game's own scripts and data. Do not rewrite game rules that the files already contain.
- `scriptapi.cpp` only translates. Real behaviour goes in C++, in small classes with one job.
- Every change passes `verify.ps1`, which runs the build, unit tests, script check, flight tests and script contract check.
  On Linux, until `verify.sh` exists: the build, `recap_tests`, and `recap_server --check-scripts` from the
  game's `Server` folder with `AI_MOBS` false in its `config.xml` (with mob AI on, some checks fail spuriously).
- Add tests with new logic. The game is for judging look and feel, not routine checks.
- Name server crashes from dumps and the linker map. Do not guess.
- Never add game files to the repository.
- Commit small steps as `feat(scope):` or `fix(scope):`.

## Where we are

| Area | State |
| --- | --- |
| Original script bytecode on Lua 5.1 | Done |
| Script adapter | Working, some placeholders remain |
| Ability scripts registered | 478 |
| Modifier scripts started by the check | 766 started, 214 fail on engine functions the server lacks (mostly level and objective ones) |
| Abilities passing the self-check | 345 of 475; hero sweep 112 of 115 (the 3 left are the Verdanth boss, which the sweep cannot set up; 2026-11-08) |
| Basic attacks in play | Working |
| Client crash while loading a level | Believed fixed (hero matrix), being tested by hand |
| Client crash on the vote screen | Believed fixed (hero matrix), being tested by hand |
| Linux (Arch, Proton) | Working: builds, `scripts/linux/install.sh` and `play.sh`, windowed only |
| Level 1-1 | Loads; teleporters work; the AI director spawns its enemies (`docs/director.md`) |
| Enemy AI | v1 brain ported from the client, behind `AI_MOBS` (`docs/ai.md`); not yet play-tested |
| Server and client agreeing on positions | Not yet: NPCs drift from the client (stage 2a, `docs/sync.md`); stops no longer snap |

## Stage 1: Combat foundation

Make every hero's abilities work.

- [x] Log calls to functions the adapter does not have
- [ ] Find the cause of the load crash, starting with the data sent at login
- [x] Build `ModifierManager`, proven first on one simple effect such as a slow
- [x] Work out the `ModifierCreated`, `ModifierUpdated` and `ModifierDeleted` packets
- [x] Implement `AddAttributeModifier` and the `nModifier` functions (see `docs/modifiers.md` for what is still approximate)
- [x] Send modifiers the ability events beyond stacking, such as `TookDamage` and `DealtDamage`
- [x] Start hero and mob passive modifiers
- [x] Stop a modifier's child threads when it ends, which `modifier_cryosboss_passive` needs
- [x] Check whether stat changes made by abilities stick after an interrupted cast (they end with the cast, even if Deactivate fails)
- [x] Clear a pooled Lua thread's old cast context, so a reused thread cannot read it
- [x] Reconstruct how attribute changes combine (the order is approximate, see `docs/modifiers.md`)
- [x] Fix projectile abilities (see `docs/projectiles.md` for what is still approximate)
- [x] Implement `CreateThreadForObject` and the remaining `nAbility` functions (only `GetWarmupAnimation` is left, used once)
- [x] Work out `TransformVector`, and replace adapter placeholders such as `IsPet` (the client has no `IsPet`: a creature owned by a creature)
- [x] Apply cooldowns and mana from scripts, instead of the fixed reply cooldown
- [x] Apply armour, resistances and damage types in `TakeDamage` (see `docs/damage.md` for what is still approximate)
- [x] Build hero stats as the client does: class base and primary stat shares, and the weapon's damage range (part stats and stat caps wait for equipping, see `docs/damage.md`)
- [x] Raise the remaining ability events, such as `Dodged`, `NearbyDeath` and `UsedAbility` (still unsent: `ModifierInstanceAdded`/`Removed`, `BreakRoot`, `StartedNewAction`, `InterruptModifier`, `PlayerSwap`, `NearbyDamage`, `LostModifierOfType`, `Incorporeal`; `CrystalBonus` in stage 5)

Done when: every hero's abilities run in play, effects apply and expire, and loading is reliable.

## Stage 2: A level that fights back

Make one level, 1-1, play properly.

- [x] Spawn every enemy from the level's marker sets, removing the temporary one per group limit:
      the director's wander points and spikes (docs/director.md); hordes and the boss are still to come
- [ ] Build level triggers (spawn, teleporter, checkpoint) on `TriggerArea` and `TriggerOccupancy`, with the hero-only modes, once-only and dwell time the client keeps (see `docs/abilities.md`), then remove the old `TriggerVolume` object class
      (security teleporters work through the game's own script; tunnel teleporters, spawn and checkpoint triggers remain)
- [x] Read how `aidefinition` and `phase` data drive enemies (the client holds the threat list, target choice,
      perception and the behavior tree; only the think loop, damage threat and leashing were server-only)
- [ ] Build the enemy AI: aggro, movement and using abilities
  - [x] The dead stay dead: no thinking, no heals that do not resurrect, bodies go after a while
  - [x] v1: the client's threat list and best target, perception, the behavior tree
        (pre-aggro hiding and the beam-in, first aggro and alert, combat idle, one action at a time),
        combat state on the blackboard, behaviors that keep their data between steps, gambits that keep
        a target, `shouldPursue`, the missing script functions, and the repaired `aggroType`
        (play-test pending; gaps in `docs/ai.md`)
  - [ ] Spawn aggro: director spawns give every hero 5 threat, as ActivateMinionSpawn does (0x9febc0)
  - [ ] `GetNPCRank` from the class's `npcRank` (now always 1, which is every 1-1 enemy's rank)
  - [ ] Combat idle 2, `targetTooFar` and the AI node cooldowns; node conditions and phase changes
  - [ ] Basic attacks with no script give no damage threat (`Instance::WeaponAttack` has no snapshot)
  - [ ] Respect crowd control (stun, sleep, fear, `Immobilized`, abilities that bind their agent)
  - [ ] Line of sight before casting (`Ability::IsAbleToHit`)
  - [ ] Ability ranks for NPCs by NPC rank and difficulty, instead of rank 1
  - [ ] `deathAbility`, and the real corpse time: the 10 s corpse removal ignores `SetDeathTimer` and
        cuts `nBehavior_Death`'s fade short
  - [ ] Threat from healing, and what to do when heroes run away (the data never leashes 1-1's enemies)
  - [ ] Check the think cadence (the blackboard hints at a staggered 500 ms)
  - [ ] NomadDrone: placed at its owner and moved by `OrbitOwner`
  - [ ] Turn `AI_MOBS` on by default once the above is solid
- [ ] Director, the rest (see `docs/director.md`)
  - [ ] Hordes: HordeTrigger, waves (`HordeDifficultyWave`), the gate teleporters and doors
  - [ ] The boss, and the boss arena's horde spawners
  - [ ] Captains and elites: the elite rolls (`EliteNPCGlobals`), affixes, `MakeElite`
  - [ ] Pick markerset variants with the client's seed (0x9d6340), so server and client agree, including obelisks
  - [ ] Tier 2 and 3 enemies: their nouns are filed under hashes and not found by name, so past difficulty 24 there are no minions
  - [ ] The repeat-visit enemy pool (planet and element configs) and planet agents in spikes
  - [ ] `DirectorState` (0x8B) for the horde and boss HUD
  - [ ] Difficulty scaling of spike budgets and enemy health and damage
- [ ] Use `directortuning` and `difficultytuning` for pacing and scaling (the exported DirectorTuning
      is misnamed: it holds OrbDifficultyScale and HordeDifficultyWave; the health and damage tables
      are in DifficultyTuning)
- [ ] Hero death, squad swapping and level failure
- [ ] Health obelisks and orb drops (the client's death drop is at 0x9ce140: challengeValue times
      OrbDifficultyScale, health or mana weighted by how hurt the players are, at most 3 resurrect orbs)
- [ ] Replace placeholder enemy hit reactions

Done when: 1-1 has its full enemy population, enemies fight back, and you can win or lose.

## Stage 2a: Server and client agree

Enemies shoot at empty spots, the hero attacks an enemy where the server does not have it and the
body snaps elsewhere on death, and the hero's attacks and animations feel out of step. The causes
are in [docs/sync.md](docs/sync.md), with evidence and the client addresses involved. In short:
the server spins chase goals around the hero, places NPCs on their goal at once, turns every stop
into a teleport, and tells the client's hero predictions the wrong things. The first three items
are small and need no navmesh.

NPC movement:
- [ ] Stop the Pursue spin: fix the slot bearing once per pursuit (or follow native MoveToRange),
      and resend only when the target moved at least 0.75 units, at most every 250 ms
- [x] Stop without teleporting: 0x91 with flags 0x20 at the current position for stops, deaths and
      behavior exits; 0x90 only for real relocations (heroes keep 0x90). Fixes the corpse snap that the
      "dead stay dead" change made happen on every death.
- [ ] Walk NPCs on the server (tracked walk like heroes, real elapsed time, speed re-read each tick),
      and have casts, reach and hits read the walked position
- [ ] Send policy: one 0x91 per walk, then 0x95 updates only; no ObjectUpdate positions while walking
- [ ] Deliver NPC turns (0x91 flags 0x42 with facing and target point)
- [ ] Simulate external velocity (knockbacks), ending in a stop rather than a teleport
- [ ] Send attributes 11, 12 and 48 in mob creates; decide when mobs are InCombat (CombatSpeed)

Hero actions:
- [ ] Swing step: use the command's userData and reply with the next step, as native 0x9de510 does
- [ ] SetAnimationState gate = the request's sync byte, scale from AttackSpeedScale
- [ ] Real busy window in the type-1 reply (commit at the hit or release, real cooldown end),
      a type-4 release when a cast ends early, and the FaceCast pose in the same batch. Do it with the
      swing step: the hit time depends on which step the server plays
- [x] Don't echo a player's own Movement back to them
- [ ] Multiplayer: send other players a hero's 0x95 partial goal too (heroes count as steered, so
      other clients walk them toward a partial goal they were never sent)
- [x] Ignore hero positions from client commands sent before a server move (`inputSyncStamp`; only
      older stamps, traced as STALE; check in play that it fires after teleports)
- [ ] Cancel (type 10) releases a held input instead of killing a running swing (check by tracing)
- [ ] Reply field +8 = ability index; cast and animation timestamps from the real clock

Navmesh:
- [x] Extract `<level>!<level>.bfx` (Levels.package type 0x1999AE0B) and NavPowerTuning radii
      (`scripts/navmesh_extract.py`, run by install.sh)
- [x] Port darkspin's BFX parser, projection, path search and funnel (`game/navmesh.*`, credited in
      LICENSES.md); layer from footprint radius
- [ ] Load the level's mesh in `Level::Load` and hand it to the instance
- [ ] Walk NPCs along mesh corners and stream the 0.75 s lookahead partial goal, as retail did
- [ ] Back nLocomotion GetClosestPosition, IsPositionReachable, FindBallRollPosition, WalkingDistance
      and director spawn scatter (client 0x9fb180) with the mesh
- [ ] Later: gates and barricades as dynamic obstacles, teleporter links between mesh islands

Measuring:
- [ ] Trace NPC positions every 250 ms, death, stop and teleport positions with their cause, and
      action command types and replies, so each fix above can be checked against the client

Done when: in a recorded fight, server and client positions of every creature stay within a step of
each other, and attacks land where they are seen.

## Stage 3: Bosses and finishing a level

- [ ] Run the `lua/objective_*` scripts, replacing placeholder objectives
- [ ] Remove the objective update sent on every tick
- [ ] Boss spawning, phases and boss obelisks
- [ ] Real level completion, ending the level from the game's rules
- [ ] Fix `template_ability_cone` dependents and the 23 failing scripts, or confirm the demo lacks their files

Done when: a level can be completed through its boss, with objectives and medals tracked.

## Stage 4: Campaign flow

- [ ] Fix the vote-screen crash
- [ ] Chain voting between levels, and moving to the next level
- [ ] Support all campaign levels in the level table
- [ ] Fix new accounts starting at 7-1
- [ ] Start new accounts properly, including the tutorial if the demo allows
- [ ] Handle levels beyond world 6, or remove them from the choice
- [ ] Test every level in worlds 1 to 6

Done when: a full run through all six worlds is possible in one session.

## Stage 5: Rewards and progression

- [ ] Read `lua/LootRules.lua` and work out the `lootpreferences` tables
- [ ] Generate items from `lootprefix`, `lootsuffix` and `lootrigblock`
- [ ] Item drops and pickup
- [ ] Real cash-out, replacing the fixed 50 DNA and medals
- [ ] Crystals from `crystaltuning`
- [ ] XP, levels and `unlockstuning`
- [ ] Save rewards to the account
- [ ] Finish hero editing and equipment

Done when: playing earns gear and progress that persist between sessions.

## Stage 6: Polish

- [ ] Balance against gameplay videos and player memory
- [ ] Test each hero through the campaign
- [ ] Decide whether the full 5.3.0.127 game is needed
- [ ] Remove the remaining upstream game data from this repository
- [ ] Update the setup guide for a first-time player

Done when: someone else can follow the README and finish the campaign.

## Server health

Bugs and gaps noticed along the way that belong to no stage. Pick them up between stages.

- [ ] Seed `utils::random`: its generator is never seeded, so every game rolls the same crits,
      catalysts and damage (the director already has its own seeded generator)
- [ ] The server crashes (segfault or abort) when stopped with Ctrl-C
- [ ] `--check-scripts` segfaults at exit (exit 139) in `lua_close`, freeing the `ScriptGaps.Record`
      closure, after every report is written; the d5dbbaf4b build does it too
- [ ] "Creating account…" never finishes in the client, though the account is saved
- [ ] The hub chat shows "Network connection was lost" from a service the server does not answer
- [ ] Noun `assetId` is written in hex but read as decimal, so it is always 0 (`utils/functions.h:68`)
- [ ] `0x3681d755!GlobalDefinitions.lua not found` at start, from the helper Lua state that only compiles abilities
- [ ] Marker rotations are degrees but are given to glm as radians in `ObjectManager::Create(marker)`
- [ ] Two self-checks fail since before the Linux work: `thorns-hits-melee-attacker-back`, `wreath-orbs-strike-and-end`
- [ ] A Linux `verify.sh` doing what `scripts/windows/verify.ps1` does (build, tests, script check, flight, contract)
- [ ] Run the data repair (`phase_gambits.py`, `serverdata_fix.py`) after the server's first extraction by itself,
      not only from `install.sh`, and give `serverdata_fix.py` tests
- [ ] Linux: fullscreen crashes under XWayland and gamescope freezes the intro, so only windowed works

## Risks

- **Enemy AI:** smaller than feared: the client holds most of the AI. The think loop, damage threat and
  leashing must be reconstructed.
- **Movement sync:** the server needs the client's navmesh and its walking rules, or NPCs and heroes keep
  drifting apart. This may be the largest technical job left.
- **Packet formats:** a wrong field can crash the client, as the ability reply did.
- **Lost formulas:** drop rates look recoverable, but cash-out and stat maths may be approximate.
- **The demo:** it may lack some content, which would mean moving to the full game.
- **Unverified data:** several files here have only been identified by name so far.

## Open decisions

- Whether the fork should enable GitHub issues.
- Whether to send fixes to the ReCap project, and the Linux and data fixes to Phylum upstream.
- When to move from the demo to the full game, if a legitimate copy can be found.
