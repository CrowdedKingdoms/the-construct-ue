# Runtime presence

ConstructPresence adds capsule player pawns and overhead names to the existing Construct shell. It uses Crowdy's entity transport and actor pool. The plugin contains one Runtime module.

## Use it

The plugin enables its ConstructShell and CrowdySDK dependencies. Select `/ConstructPresence/Framework/BP_ConstructPresenceGameMode` for a map that uses presence. The startup map already selects it. Configure the application's connection through the SDK's normal Studio workflow.

The GameMode starts the player as a spectator. The shell admits the native presence pawn after sign-in and connection are ready, and releases it on sign-out or access end. The native presence character uses Unreal Character Movement for walking, gravity, falling and one grounded jump. Its own Enhanced Input context maps WASD/arrows and the left stick to ground movement, mouse/right stick to look, and Space/the bottom gamepad face button to jump. Looking up or down does not turn walking into flight. The shell's separate preview camera remains a free camera.

## Visuals and names

The capsule consists of three native components referencing the Engine Cylinder and Sphere meshes, with a shared unlit material under this plugin. The references live on the native default components so subclasses retain the assets needed for cooking. There is no Engine BasicShapes capsule mesh. The native capsule collider has radius 35 and half-height 80, blocks world geometry and ignores other pawns. Visible meshes have no collision. A first-person camera follows local look at capsule-relative height 64. The movement defaults are walk speed 600 uu/s, jump velocity 420 uu/s and normal world gravity.

After the SDK assigns local ownership, the pawn reads the signed-in player's gamertag once through `UCrowdyPlayers::GetPlayerName`. An empty name or failed lookup uses `Player`. The owner hides its own tag. Remote tags face the local camera.

`PlayerName` is CrowdyState with a heartbeat and a parameterless notify. It is client-asserted display data; authoritative rules must not use it. Pose is sent by the SDK's default continuous entity executor, with the SDK's normal interpolation on remote copies. The pawn declares no second pose field or Unreal network replication.

The startup map includes an 80 by 80 metre collision platform, Sky Atmosphere, a movable directional sun, Exponential Height Fog and a skylight. It retains its original PlayerStart. The Engine's default text material is lit, so custom maps also need scene lighting for readable name tags.

Only the locally SDK-owned and locally possessed character runs Character Movement. Remote copies keep movement inactive and receive their entire pose from the SDK. Ownership loss, restart, unpossession and teardown reapply this rule. The shell input gate blocks jump as well as movement/look and clears a queued jump when a modal takes input.

## Player name

Press F2 or the gamepad View button to open/close the runtime player-name form. Its upper-right name badge also opens it when pointer input is available. It also opens automatically after a successful profile read if the account has no gamertag. Enter a gamertag and its public identifier, then choose Save. This updates the signed-in Crowdy account through `UCrowdyPlayers::SetPlayerName`; it does not create a local display-name override. A new empty profile starts with an editable `0001` identifier. Existing identifiers are preserved.

The confirmed returned profile updates the current pawn and its CrowdyState name. Refresh reads the account again through `GetPlayerName` and applies the returned name. Errors leave the existing name and draft intact. A failed initial read offers Refresh rather than assuming the profile is empty. The server owns name availability and account validation.

The form uses the shell's runtime styles and input gate. Opening it blocks movement/look/jump, and closing releases only its own modal. Closing while a save is pending hides the form; it does not cancel the SDK operation. A reply may update the same current signed-in pawn, but cannot reopen the form or take focus. Ownership loss, unpossession and teardown invalidate the receiver. SDK legal input keeps priority.

## Pooled copies

The game-world subsystem disables the SDK's owner echo and listens to its appearance, departure and timeout events. On appearance it clears both the reused pawn's name field and its rendered text. This lets the next identical name notify correctly. The SDK owns spawning, interpolation, hiding and pooling; this plugin does not replace those systems.

The native pawn class stays loaded with the runtime module. A project that changes the wire-spawned pawn to a Blueprint subclass must also follow the SDK's documented class-preloading requirements.

## Extend it

`AConstructPresencePawn` derives from Unreal's `ACharacter`. Its protected `PlayerName` and virtual `OnRep_PlayerName` let native subclasses observe the name without replacing SDK transport. Call the parent notify when extending its presentation. Scripted native subclasses can override the protected `ShouldUseLocalCharacterMovement()` to return false, keeping their own transform writer in control through restart and ownership events.

`AConstructPresenceGameMode` derives from `AConstructShellGameMode`. Its Blueprint contains only defaults, with no graphs; the shell continues to own login, UI input and pawn admission.

## Checks

Run `Automation RunTests Construct.Presence` in a Development automation worker. The focused tests exercise the actual pawn's successful and fallback name application, pooled-name reset, same-name reuse, owner visibility, rejected callbacks, native mesh/material references and movement/camera defaults. The movement tests additionally exercise actual Character Movement collision sweeps, gravity, landing, jumping, single-jump limits, proxy/restart gating and queued-jump cancellation in isolated physics worlds.

These isolated tests do not establish multiplayer transport, cooked rendering or hardware input. Those require the project's live runtime and packaged checks.
