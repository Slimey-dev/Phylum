# Enemy AI

An NPC's AI runs on the server only. Darkspore.exe 5.3.0.103 holds the decision code: the threat
list, the perception circle, target choice, timed stimuli and the creature behavior tree. The client
never runs any of it. Brain init (0xa22a50) and tree creation (0x9d6860) return early when IsClient
(sim + 0x1b) is set, and the AI tick (0xa21800) has one caller, inside ability start (0x9e11aa). The
client only mirrors what the server replicates: `mVisible`, the five AgentBlackboard fields,
attributes and animations.

The code is in `object.cpp` (class `AI`, which reads the world and acts), `aggrolist.h` (the threat
list), `aibrain.h` (stimuli and the tree's choice) and `aitargeting.h` (the perception tests).
`aggrolist` and `aibrain` are pure and unit-tested (`tests/aggrolist_test.cpp`,
`tests/aibrain_test.cpp`).

## Who runs what

| Agent | AI_MOBS off (default) | AI_MOBS on |
| --- | --- | --- |
| Mob (NPC that is not a hero's summon) | `TickWithoutGambits`: the first-aggro cast at the first player in aggro range, then facing it | The brain below |
| Hero summon (`IsHeroSummon`) | `Think`: gambits at the nearest hostile in aggro range | Same |

`RunsGambits` (aicontrol.cpp) is still the gate, and `AI::UsesBrain` is `RunsGambits` and not a
summon. Summons keep their nearest-hostile targeting, which was tuned against Sage's saplings and
Tork's pets. In the client they had a threat list too, so moving them onto `AggroList` is open
work. A dead agent's AI stops (the corpse check in `AI::OnTick`), as before.

## The brain, one think

The brain thinks every 250 ms. The client's blackboard keeps 500 and rand(500) (+8, +0xc), which
no client code reads, so the period is a judgement call. A mob's first think is put off by
`id % 250` ms, so a group spawned together does not think on one update.

1. **Stimuli decay** by the time since the last think (0xa64810).
2. **Lost targets are dropped**: an entry whose object is gone, marked for deletion, dead (HP 0)
   or in the squad reserve. The client never drops an entry, so this is INFERRED. There is no
   distance leash: every 1-1 enemy has `dropAggroRange` FLT_MAX and `dropDelay` 0, and the client
   never reads either field.
3. **Perception** (`AI::Perceive`). A hero, or something a hero made, that comes inside the mob's
   aggroRange is listed with 5 threat and flag 2, the first time it comes in. These are the client's
   Aggro_Trigger semantics (0xa22620), checked through 0x9d95c0: a live, tangible combatant on
   another team. The trigger volume itself was created by the server, and no asset names it, so
   its size is INFERRED to be aggroRange. Leaving and coming back in triggers again.
4. **Combat state.** The best target is the first entry that passes 0x9e3e50. The agent is in
   combat when it has a best target and `HasAggroed` (0x9e3c70). The blackboard's `mTarget` and
   `mbInCombat` follow, and the change is sent at once (AgentBlackboardUpdate 0x99), before any
   turn, walk or cast this think makes. The clients use it for head look-at (0x509a90) and for
   CombatSpeed versus NonCombatSpeed (0x9e49d0).
5. **The tree** (`ChooseNode`), the first child that wants the agent:

| Child | Client | Brain |
| --- | --- | --- |
| Death | [1] 0xa21160 | `nBehavior_Death`, run once when HP reaches 0 (creature tree only) |
| Running cast | [2] 0xa20e70 -> 0x9dee30 | An ability of the agent's that needs it (`requiresAgent`) still runs: nothing else starts, no gambit and no pursuit, and the idle ends |
| First aggro | server-only | Stimulus 0x20: cast firstAggroAbility, or firstAggroAbility2 on a secondary start |
| First alert | server-only | Stimulus 0x200, once `HasAggroed`: cast firstAlertAbility |
| Subsequent aggro | server-only | Stimulus 0x80: cast subsequentAggroAbility |
| Combat | [4], [7]-[9] | Gambits (`GambitRunner`, `AI::Pursue`); between casts combatIdle, else `nBehavior_CombatIdle` |
| Idle | [10] 0xa21e20, 0xa214d0 | preAggroIdle (preAggroIdle2 on a secondary start) until it has aggroed, then passiveIdle |
| Wander | [11] | `nBehavior_Wander` when the idle node names nothing |
| Rest | 0x01181570 | Anything that is not a creature (nounType 0x06c27d00) has the short tree: no idles, no wander, no death behavior |

A reaction needs a best target, because it faces it. A reaction with no ability is used up, and
the next child gets the same think. Casting a reaction leaves the idle first. That is how the
client reveals a mob: an ability start ticks the tree (0x9e11aa), node [2] preempts the idle, and
`nBehavior_Invisible`'s exit makes the mob visible and tangible as the beam-in starts.

## Ported from the client

| Code | Client | What it does |
| --- | --- | --- |
| `AggroList::Add` | 0x9e4640 | amount × (1 + AggroIncrease) × (1 − AggroDecrease) of the source, at least 0.01; a listed source adds to its entry, a new one is listed; aggroType 2 ignores it |
| first entry | 0x9e471b-0x9e47da | The first entry ever (empty list, latch +0x555 clear) posts FirstAggro for 10 s with flags 2 or 4, or FirstAlert for 10 s with flag 8 and a (U × 0.75 + 0.5) s delay; the latch is then set |
| `AggroList::Alert` | 0x9e4870 | 5 threat, aggroType 0 only, only for an unlisted source; FirstAlert on the first entry ever, with no delay (an ally alerted on an agent's first aggro gets the AddAggro alert delay, INFERRED) |
| `AggroList::Get` | 0x9e3e10 | 0 for an unlisted source |
| `AggroList::BestTarget` | 0x9e3e50 | First entry whose object resolves, is seen (StealthDetection > 0, or no blackboard, or not stealthed), has a blackboard with mbTargetable, is a combatant and is not Intangible. HP is not checked |
| `AggroList::HasAggroed` | 0x9e3c70 | The latch, except during an alert's delay |
| `AggroList::IgnoreFirstAggro` | 0x9e3cc0 | Sets the latch; the AI clears stimuli 0x2a0 |
| `AIStimuli` | 0xa64690, 0xa64780, 0xa64810, 0xa64540 | Eight timed slots; a post replaces the same mask, else a free slot, else the one with the least time left |
| `PerceptionRadius`, `TouchesCircle` | 0x9e4040, 0xa0aea0 | max(aggroRange, alertRange), 20 without an NPC class; strict test radius > \|p − c\| − r |
| `RollSecondaryStart` | 0xa22a50 (0xa22ba1) | U < useSecondaryStart, at brain init |
| `IdleFor` | 0xa21e20, 0xa214d0, 0x9e3d00 | The idle node's behavior |
| `CombatIdleFor` | 0xa211c0, 0xa213e0 | combatIdle, else `nBehavior_CombatIdle` |
| tree order | 0x011814e8 (npc_tree dump) | The priority order of `ChooseNode` |

## Approximate and INFERRED

- **Threat order.** The client appends and takes the first valid entry; it never sorts. Entangle
  (`ability_entangle.lua`) adds the target's threat minus the caster's, plus 5, which only works if
  the highest threat wins. So `AggroList` keeps entries highest threat first, and a tie keeps the
  order the entries were listed in (the client's own order).
- **Damage threat.** No client caller passes flag 4. `Object::TakeDamage` adds threat equal to the
  damage it took, with flag 4, which reacts as a first aggro does. The threat goes to the attacker
  snapshot's owner, or to that attacker's aggro delegate (SetAggroDelegate; the client copies the
  delegate into the snapshot, 0x9e3140). Hits from the same team give none.
- **Ally alert.** When an agent first aggroes, every live ally (same team, running the brain)
  within its alertRange gets `AlertObject` for the source, with the 0.5 to 1.25 s alert delay. Alerted
  allies alert no one further.
  The client has AlertObject and alertRange, but nothing that joins them.
- **In combat** needs `HasAggroed`, so an alerted ally waits out its 0.5 to 1.25 s delay in its
  idle before it reacts. The delay is only ever read by HasAggroed.
- **Reactions.** The client reads none of firstAggroAbility, firstAggroAbility2, firstAlertAbility,
  subsequentAggroAbility or the OverrideFirstAggro slot (+0x584).
  - firstAggroAbility2 goes with the secondary start, instead of following firstAggroAbility. In the
    data, all 35 definitions that name it name FirstAggro_FaceTarget, always with
    preAggroIdle2 = nBehavior_Wander (a mob that is already visible). firstAggroAbility beams,
    flies or digs in an invisible one. Darkspin pairs them the same way. A secondary start with no
    firstAggroAbility2 plays nothing.
  - OverrideFirstAggro's ability replaces the first aggro or the first alert, once.
    ability_shadowteleport.lua sets it right after AlertObject.
  - SubsequentAggro (0x80) is posted when a list that emptied after a fight lists someone again.
- **Attackers.** A source's `mNumAttackers` is the number of live agents running the brain whose
  list holds it. The client records the attacker list (0x9e4610) but only the server ever set the
  count.
- **One action at a time** counts a cast while its script's thread runs, up to 10 s.
  RunningCasts is the stand-in for the client's ability instances, which hold their agent until
  they are released (+0x165). The cap keeps a script that never ends from freezing its agent.
- **Hidden spawn.** A mob whose first idle is `nBehavior_Invisible` is created with `mVisible` false,
  so ObjectCreate carries field 16 = 0. Its first update starts the behavior, which adds Intangible
  and InvisibleToSecurityTeleporters; its exit shows the mob again. This needs AI_MOBS, a
  creature that is not a pet class, and the behavior defined in the scripts. Until that first
  update (one server tick) the hidden mob is not yet Intangible.
- **Perception center.** A pet (NonPlayerClass.playerPet) or a hero's summon perceives around its
  owner. The client moves a pet's circle ahead of the owner by the camera's yaw (+0x58c). The
  server has no camera, so it uses the owner's position.

## The 1-1 enemies

| Enemy | Start | First aggro | First alert | Subsequent | Idles |
| --- | --- | --- | --- | --- | --- |
| ZelemBasicRanged | Invisible | FirstAggro_Anim | FirstAggro_FaceTarget | none | passive Idle, combat CombatIdle |
| ZelemBasicHybrid | Invisible (20%) or Wander (80%) | Anim, or FaceTarget on a Wander start | FirstAggro_Anim | none | combat Wander, passive Idle |
| ZelemBasicRepair | Idle | FirstAggro_ActivateRobot | ActivateRobot | SubsequentAggro_ActivateRobot | passive IdleWithIntro |
| ZelemSpecialHaster | Wander | none | none | none | passive Idle |
| NomadSnipe | Invisible (20%) or Wander (80%) | FirstAggro_BeamIn, or FaceTarget on a Wander start | BeamIn | none | no passive: Wander after a fight |
| NomadWithDrone | Idle | ActivateRobot | none | SubsequentAggro_ActivateRobot | combat Idle |

All six: aggroType 0, one AI node, faceTarget, no combatIdle cooldowns, no targetTooFar, and
dropAggroRange FLT_MAX.

## What the brain relies on

Behaviors, through BehaviorRunner's Run and Exit: `nBehavior_Invisible`, `nBehavior_Idle`,
`nBehavior_IdleWithIntro`, `nBehavior_Wander`, `nBehavior_CombatIdle` and `nBehavior_Death`, plus
whatever idle a definition names. The brain also needs the steps of one behavior to share
nThreadData (Invisible's exit removes what its start added; Wander walks to the point its start
picked). It needs nGameObject.GetPositionWithinRangeOfObject for Wander,
SetAnimationStateToEnterPassiveIdle for IdleWithIntro, and nAgent.GetBestTarget and
nLocomotion.TurnToFace for CombatIdle.

Lua functions that should be bound to the AI (scriptapi.cpp):

| Lua | AI |
| --- | --- |
| nGameObject.AddAggroForObject(npc, source, amount, reason) (0xa02bb0) | `AddAggro(source, amount, AggroFlags::Script, reason)` |
| nGameObject.GetAggroForObject(npc, source) (0xa02c20) | `GetAggro(source)` |
| nGameObject.AlertObject(npc, source) (0xa02c70) | `AlertObject(source)` |
| nGameObject.IgnoreFirstAggro(npc) (0xa02d70) | `IgnoreFirstAggro()` |
| nGameObject.OverrideFirstAggro(npc, ability) (0xa02cf0) | `OverrideFirstAggro(ability)` |
| nGameObject.SetAggroDelegate / GetAggroDelegate (0xa02ee0, 0xa02f60) | `SetAggroDelegate`, `GetAggroDelegate` |
| nGameObject.IsStimulusActive(npc, mask) (0xa030f0) | `IsStimulusActive(mask)` |
| nAgent.GetBestTarget (0xa00d80) | `GetBestTarget()`, which filters; the first id of `GetAggroList` does not |
| nAgent.GetTargetsOnAggroList, HasTargetsOnAggroList | `GetAggroList()` |

## Gaps

- **Spawn Aggro.** ActivateMinionSpawn and ActivateLieutenantSpawn (0x9febc0) give every deployed
  hero 5 threat (flag 2) on the new creature. The director's own spawns get theirs from perception.
- **Death ability.** deathAbility and deathCondition (node [0], 0xa21aa0) are not cast, and
  finishOnDeath does not hold off nBehavior_Death. No 1-1 definition has one.
- **combatIdle2, targetTooFar and the cooldowns** of combatIdle, combatIdle2 and targetTooFar are
  not run. combatIdle2 needs its condition (0xa33170), and every 1-1 cooldown is 0. targetTooFar
  is approximated by GambitRunner's TooFar walk.
- **Node conditions and phase changes**: only the first AI node with a phase runs, and every 1-1
  enemy has one node.
- **Blink and ReleaseAgent.** A cast counts as running until its script's thread ends, not until
  the ability releases its agent (nAbility.ReleaseAgent, timetorelease).
- **Weapon attacks without a script** (Instance::WeaponAttack) pass no snapshot and give no threat.
- **NomadDrone** names no idle, so it wanders, which may fight its OrbitOwner locomotion.
- **Summons** keep their nearest-hostile targeting, so taunts and Entangle do nothing to them.
