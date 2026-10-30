# Construct Chat

This Runtime plugin installs one chat component per local PlayerController in Game and PIE worlds. It preserves an existing component and adds none to remote controllers. Native local-player/controller events handle late assignment and replacement without permanent polling. Components complete local setup if an authored component began play before its controller became local.

The component owns its shared-theme panel. Signed-in players see the last 100 lines at the lower left. Enhanced Input Enter opens the composer; Enter in its text field sends and closes, and Escape cancels. The shell input gate owns movement/look blocking and focus. Sign-out, access end, account changes and teardown clear history and pending requests. The short setup retry stops after presentation/input is ready or ten seconds.

Nearby chat is the default. It calls the SDK's UCrowdyProximityChat::SendProximityText(Text, 4), receives OnProximityTextReceived and shows OnProximityTextRefused as a late status notice. Sent means the line left this client, not that another player received it. Nothing copies SDK routing or changes chunk size. Refusals never cause automatic resends.

A received nearby line takes its sender name from the presence pawn returned by UCrowdyEntitySubsystem::FindEntity(SenderEntityId), through AConstructPresencePawn::GetPlayerName(). Names are bounded to 64 UTF-16 units and one line, with Player as the empty or missing-pawn fallback. No profile lookup or name cache exists. Both the actor id and the replicated name are client assertions used only for presentation.

The explicit **Use channel chat** action selects ordinary app-wide channel chat; **Use nearby chat** returns to proximity mode. Switching modes closes the composer and clears the visible history so lines from different audiences are not relabelled. Set DedicatedChannelName (default construct_chat) to a channel the account already belongs to. The component never creates or silently joins channels or substitutes the session channel.

The channel fallback uses the documented UCrowdyChannels GetMyChannels, OnChannelMessageReceived, PublishChannelMessage and CanPublishChannelMessage APIs. Its construct.chat.v1 envelope is bounded to 2048 bytes. Channel notifications contain an actor UUID rather than a player UserID, so received fallback rows show Player. Channel publish submission is not represented as delivery confirmation.

All outgoing text is trimmed, folded to one line and bounded to 240 UTF-16 units without splitting a surrogate pair. Incoming text stops at its first line break and is bounded to the same length. Slate displays literal text. Membership replies are accepted only for the current request, session generation and signed-in user/app identity. Backend error text, chat text and identifiers are not written to product logs. Runtime shutdown unbinds both channel and proximity delegates from the GameInstance that owned those bindings.

Automation includes Construct.Chat.PayloadBounds, BoundedHistory, SessionIsolation, LocalControllerInstallation, EndPlayReleasesComposer, ControllerReplacementCleanup, ProximityPresenceNamesAndBounds and ProximitySignedOutDelegateIsolation. Tests exercise real Engine controller events, EndPlay, a registered presence pawn, SDK message structs and the public SDK delegates without inventing authentication state. Result presentation fixtures do not send real chat. Live near/far delivery still requires the agreed paired-client run.

After the composer closes or a mode/Refresh button is used, its owning viewport regains logical keyboard focus on the next world tick if gameplay is still allowed. Reopening, another modal or teardown cancels or blocks that request.
