# Server and client sync

Why the server and the Darkspore client disagree about where creatures are and what heroes are
doing, and how to fix it. Found 2026-11-27 by reading Phylum, decompiling Darkspore.exe 5.3.0.103,
replaying `script-live.txt` traces, and comparing with darkspin's research (MIT). Client addresses
are virtual addresses in Darkspore.exe. "Inferred" marks a conclusion that was not checked directly.

## How the client moves creatures

- Every creature except the player's own hero is walked by the client's NavPower mover
  (tick 0x9ea1a0 -> 0x9e8700 -> Mover::GotoPos 0xa5b0e0) toward the **partial goal**
  (locomotion +0x154), along the client's own navmesh path. The object's position is copied
  from the mover every frame, so ObjectUpdate positions are overwritten on the next frame.
- Walk speed is (InCombat ? attribute 12 : attribute 11) * (1 + attribute 48) (0x9e49d0),
  the same formula as the server's GetModifiedMovementSpeed.
- ObjectPlayerMove 0x91 (0x5393d0) replaces the whole command (flags, goal, facing, stop
  distances, target) and resets the re-path timer. It is dropped for the local hero unless busy.
- LocomotionDataUnreliableUpdate 0x95 (0x539810) only moves the goal and partial goal.
- ObjectTeleport 0x90 (0x5392f0) resets locomotion and warps the transform: a hard snap.
- Flag 0x20 in a 0x91 stops the mover where it is (0x9f8de0), with no snap.
- The retail server computed the partial goal as a point 0.75 * speed ahead of its own position
  along its own path (0x9e9770), and re-planned every 0.2-0.3 s. Remote clients walked to that.

## What Phylum does wrong

Ordered by how much of the visible desync each one causes.

1. **Pursue spins its goal around the target.** AI::Pursue measures the direction from the
   agent's server position, which MoveObject has already set to the previous goal. It then turns
   that direction by (id%4 - 1.5) rad again on every 250 ms think while the hero moves. Ranged mobs
   hop about 21 units every 250 ms, melee about 4 units (`object.cpp` Pursue). Nearly half of all
   goal hops in the traces are about ±86°.
2. **NPCs are put on their goal at once.** MoveObject's non-hero branch calls StartWalk (an
   untracked timer) and then SetPosition(goal). Casts, range checks, hero facing and hits all use
   a spot the client mob has not reached. The server is more than 3 units off in about 45% of
   moving time (p90 15 units, max 33).
3. **Every stop is a teleport.** MoveObject treats goal flag 0x20 as a teleport. Death (since the
   corpse change), behavior exits, Lua Stop and ClearExternalVelocity all send ObjectTeleport to
   the server's guess, so creatures snap. All 15 deaths in the 18:15 build snapped.
4. **A complete 0x91 on every goal.** Up to 4 times a second per mob. darkspin measured the client
   mover stalling under this and falling 28 units behind.
5. **NPC turns never reach the client.** The per-tick flush sends a reflection of mDataBits, which
   never holds goal, partial goal or facing. Mobs shoot while facing their walk direction.
6. **No navmesh.** Walks are straight lines, IsPositionReachable is always true and
   GetClosestPosition returns its input. Some client paths are 2-8 times longer than the
   server's straight line, and wander goals land up to 20 m off the mesh.
7. **External velocity is not simulated.** The client slides knocked-back mobs, the server does
   not move them, and the end of the push teleports them back.

Hero actions:

8. **Animation step disagreement.** The client predicts a basic attack's swing variant from the
   userData of the server's last reply. Phylum echoes the client's value, so the client always
   predicts step 0 while the server cycles 0..n. 2 of 3 Sentinel and 4 of 5 Rogue swings change
   variant mid-swing.
9. **SetAnimationState gate is 0.** The client skips a server animation for its own hero only
   when the gate equals its action sync byte (1..255). Phylum sends 0, so every cast animation
   restarts. The scale is always 1.0, ignoring AttackSpeedScale.
10. **No busy window.** The type-1 reply sets commit = start (timeImmobilized 0), so the client
    accepts move clicks mid-windup while the server keeps swinging. The FaceCast pose then arrives
    on the next tick and turns the hero back.
11. **Early held requests.** Melee basics pay cooldown at the hit, but the reply says the cooldown
    ends at accept + cooldown. The client asks early, the server holds the request unanswered,
    and the client blocks movement input meanwhile.
12. **Movement echoed to its own client.** Each player Movement goes back to the owner as 0x91,
    which re-installs old goals while the hero is busy.
13. **inputSyncStamp never checked.** Phylum bumps it on teleports but accepts every command
    position, so stale positions pull the hero back after a blink or teleporter.
14. **Cancel (type 10) kills a running swing.** darkspin reads type 10 as releasing a held input.
    Inferred; needs command-type tracing.
15. Small: reply field +8 is hardcoded to 1 instead of the ability index. Cast and animation
    timestamps use the 50 ms game clock.

## Fix order

The Stage 2a list in PLAN.md follows this order. Items 1-3 are small and need no navmesh.

1. Stop the Pursue spin: fix the slot bearing once per pursuit, or follow native MoveToRange
   (goal = target, stop distance = radii + adjusted range). Resend only when the target moved
   at least 0.75 units, at most every 250 ms.
2. Stop without teleporting: send 0x91 with flags 0x20 at the current position for stops,
   deaths and behavior exits, and 0x42 with facing and target for attack turns. Keep 0x90 for
   real relocations only.
3. Walk NPCs on the server (StartTrackedWalk, as heroes do), advanced by real elapsed time, with
   speed re-read every tick. Casts, reach and hits read the walked position.
4. Send policy: one 0x91 when a walk starts or changes kind, then 0x95 only, when the point moved
   at least 0.75 units and at most every 250 ms. No ObjectUpdate positions for walking NPCs.
5. Deliver NPC turns (0x91 flags 0x42, a normalized facing, target point).
6. Hero casts: step from userData and the next step in the reply, gate = request sync byte,
   attack-speed scale, real commit and cooldown times, a type-4 release, and the pose in the same
   batch as the reply. Don't echo the owner's own moves. Check inputSyncStamp.
7. Navmesh: extract `<level>!<level>.bfx` (Levels.package type 0x1999AE0B), port darkspin's BFX
   parser, projection, A* and funnel, pick the layer from the footprint radius (radii 0.71, 1.06,
   1.24, 1.41, 1.59, 1.77, 3.89). Walk NPCs along the corners, stream the 0.75 s lookahead
   partial goal, and back the nLocomotion queries and director spawn scatter with it.
8. Simulate external velocity. Send attributes 11, 12 and 48 in mob creates.
9. Tracing: NPC positions every 250 ms, death, stop and teleport positions with their cause,
   command types and replies, so each fix can be measured against the client.

## Open questions

- What locomotion +0x160 (the replicated goal, written only by a 0x94 field 5) starts as. It
  sets the client's stop distance for remote creatures.
- The retail send rate and reliability of 0x95. 100-250 ms is a judgement call.
- Whether client movers avoid each other and the hero (repulsors, flocking). That would cause
  crowd drift the server cannot copy.
- Whether retail mobs walked at CombatSpeed in fights. Phylum never sets InCombat, so they walk
  at NonCombatSpeed.
- What type 10 is sent for, and where native code resets its per-agent swing step after idle.

The investigation's scripts and measurements (orbit.py, cursor.py, clientsim.py, the nav/bfx.py
navmesh prototype, decompiles and findings.md) are kept outside the repo, next to the checkout in
`../research/sync/`. They contain decompiled game code, so they stay out of the repo.
