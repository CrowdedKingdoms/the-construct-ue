# Construct Voice

Optional runtime voice controls over the Crowdy SDK. Enable the plugin alongside ConstructShell, then open Project Settings > Plugins > Crowdy Construct > Voice and enable **Enable Voice**. Start a new PIE or game world after changing this setting.

Voice is disabled by default. While disabled the installer adds no controller component, registers no controller observers or timers, and creates no panel or input mappings. Manually attached ConstructVoice components remain inactive as well. This controls the Construct integration; it does not change independent SDK use or the SDK's own subsystem lifecycle.

When enabled, the world subsystem adds one component to each local PlayerController, preserves an existing component, and adds none to remote controllers. The component owns its panel, event bindings and Enhanced Input mappings, and removes them when it ends play. The existing Construct startup map can be used; no special HUD or gallery plugin is required.

Both player choices start off. Enable push-to-talk in the runtime panel, then hold **V** during gameplay to speak and release it to stop. Enable listening separately to hear incoming voice. Menus, application/viewport focus loss, paused access and sign-out prevent capture. After an interruption, release the held talk key before pressing it again. Status comes from the SDK's microphone state and refusal events.

The Construct requires Enhanced Input and the shell's runtime input gate. SDK capture, codec, transport, device handling and playback remain owned by Crowdy. No capture device is opened or playback started until the player opts in.

The component applies a one-chunk voice range through UCrowdySDKSubsystem::SetVoiceChatDistance(1) immediately before requesting microphone capture. The SDK owns routing from the local pawn's live position. Enabling the project setting or the player's push-to-talk choice alone does not change SDK range or start capture.

Checks cover disabled setup, local-only installation, duplicate prevention, manual-component policy, input/release latching, teardown and session cleanup. Compilation, automation, real PIE and live two-client audio are separate verification steps; see the internal implementation record for results.

Voice UI actions return logical keyboard focus to their own viewport when gameplay is allowed, so push-to-talk does not require an extra scene click. Controller installation publishes ownership before activation callbacks and preserves a replacement controller selected by such a callback.
