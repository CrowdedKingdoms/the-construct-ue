# Construct runtime shell

The project now opens `/Game/Construct/Maps/L_ConstructStart` by default. Press Play to use the runtime login screen. Default GameInstance and GameMode reference the existing ConstructShell Blueprint assets; the startup map retains its matching map-local override. Existing example maps and their overrides are preserved. SDK app/backend configuration is unchanged.

The screen uses the approved centred near-black/gold design, with existing Crowdy SVG provider/mail/disclosure icons. At runtime only providers returned by the SDK are shown. Google and Discord in the design preview are examples, not a hardcoded availability list. Additional enabled providers scroll inside their section; the whole form can scroll on unusually small windows.

## Sign-in behavior

- Password Sign In and Create Account use the email/password fields. Registration also signs the new account in.
- Email me a sign-in link uses the email field and waits for the SDK's callback. Social sign-in opens the system browser through the SDK.
- Saved-session restore happens when the map starts. A first launch without a saved session shows the ordinary form.
- The form disables repeated submissions while waiting. Cancel sign-in calls the SDK's full Logout operation, which cancels the pending flow and clears its session.
- SDK errors and access-ended messages appear inside the card. Password text is cleared after submission and on teardown; this plugin never persists or logs credentials or tokens.
- The SDK supplies its own legal/age prompt. The shell neither accepts it nor duplicates it.
- Successful sign-in hides the form and leaves connection/paused status plus Sign Out. SDK connection, token refresh and reconnection remain SDK-owned.
- Connection settings is informational. App/backend settings stay in Crowdy Studio; this plugin changes none of them.

Use this project's existing configured Crowdy app and an account with access. A successful C++ build or local image render does not prove live account access. No server code, SDK code or app configuration is included in this package.

## Reuse

`UConstructSignInWidget` is a Blueprintable UUserWidget backed by native Slate for consistent layout. Use `ConstructShellHUD`/`ConstructShellGameMode` on a map, or create the widget under an existing HUD and route input there. The plugin contains one Runtime module and depends on documented Crowdy runtime modules only. Its login UI and authentication are tested by playing the startup map in PIE.

The source of truth for layout is `Source/ConstructShell/Private/ConstructSignInWidget.cpp`. Icons are under `Resources/Icons/`, copied from the existing SDK resource set and staged for packaged runtime use.

## Validation

Open `/Game/Construct/Maps/L_ConstructStart` and use Play In Editor to test the actual runtime login widget and public SDK authentication flow. Verify field validation, provider discovery, magic-link or enabled social sign-in, browser return, cancellation, saved-session restore and Sign Out. Check focus, duplicate submissions and visible errors in that same game session.

The screenshot-preview commandlet and its editor module have been removed. The plugin contains runtime UI only. Current verification includes PIE and a cooked Win64 Development runtime; a development automation test can drive the actual login widget without desktop input.

## Runtime controls

The login-only map uses AConstructPreviewPawn: the Engine floating movement/collision behavior with default legacy movement bindings disabled. Its own Enhanced Input context handles movement, mouse look and gamepad controls; it is removed on unpossess/EndPlay. The project input configuration selects EnhancedPlayerInput and EnhancedInputComponent and disables legacy controller scales. Per-action modifiers preserve the existing look gains. Generated device-axis, gesture, touch and unrelated template overrides were removed.

Shared modal state blocks movement/look while sign-in, SDK legal UI or application/access conditions require it. Standard text editing and widget buttons remain Slate/UMG controls. Other gameplay examples are separate from this shell package.

After the migration, a hidden PIE worker verified fresh SDK magic-link sign-in and connection, automatic session restoration, rapid logout with movement/look blocked, and a subsequent signed-out restart. It also verified the preview pawn and EnhancedPlayerInput classes. No desktop input was sent.

## Blueprint entry points

The new assets retain the documented shell paths:

- `/ConstructShell/UI/WBP_ConstructSignIn` and `WBP_ConstructStatus`
- `/ConstructShell/UI/BP_ConstructHUD`
- `/ConstructShell/Framework/BP_ConstructGameMode`, `BP_ConstructPlayerController` and `BP_ConstructGameInstance`

The startup map uses the Blueprint GameMode, which references the Blueprint HUD/controller; that HUD references both widget Blueprints. The existing GameInstance asset is now the project default. UConstructShellLifecycleSubsystem binds the SDK events after its dependencies initialize and removes its bindings on shutdown. The widget Blueprints inherit native Slate layout: their visual hierarchy is authored in C++, not as a UMG Designer tree. The sign-in operations are callable from Blueprint. This package therefore uses native parents rather than being a content-only plugin.

## Lifecycle and diagnostics

SDK connection, timeout, address and pause notifications immediately refresh the existing HUD/input policy, with polling retained as a fallback. The GameMode waits through pause/legal holds and retries on resume or consent completion. Successful logout and access loss release the authenticated pawn; explicitly tagged local-preview pawns remain available.

The shared input gate observes pawn changes and restart notifications. It repairs missing movement/look blocks after Engine ClientRestart without stacking existing counters. It detaches those observers on controller replacement and shutdown. HUD EndPlay releases management, owned input blocks and old session readiness while preserving modal registrations from other screens. Later modal/focus changes cannot capture input on a map without the shell HUD.

Shell logs contain fixed issue categories and normalized ISO access deadlines. Arbitrary backend messages remain in the runtime UI, rather than being copied into durable logs with potential credentials or identifiers. The shell still observes the SDK token-refresh event; it never refreshes or stores tokens itself. Canceling a pending login or restore uses the SDK cancellation API; explicit Sign Out remains a separate logout operation.

## Earlier verification

On 2026-10-28, the Editor and Game Development targets built successfully. Both `Construct.Shell.SignIn` automation tests passed. A fresh process reloaded all six Blueprint assets and checked their class bindings and startup map. Subsequent PIE checks verified real magic-link sign-in/browser completion, connection, saved-session restoration and Sign Out. On 2026-10-29 the Editor build passed, all five focused shell tests passed, and 39 background PIE checks passed for configured startup, signed-out/authenticated map travel, fresh magic-link login, saved-session restore, logout and cleared-session restart. A separate 18-check PIE regression exercised real Engine ClientRestart under a modal and preserved signed-out Holodeck preview through an actual SDK logout. No desktop input was sent. OAuth is deliberately skipped. Password/registration were not live-tested; no account was created. Full/paused/access-ended/reconnecting presentation and input decisions were covered through the production policy helpers, not deliberately induced live server conditions. That earlier two-minute connected observation saw no natural token-refresh event; packaged execution was deferred at that time. The current verification below supersedes those limits.

The actual rendered widget is [RuntimeLogin.png](Documentation/RuntimeLogin.png); the approved image is [ApprovedLoginConcept.png](Documentation/ApprovedLoginConcept.png). This image is retained as historical visual evidence; the removed preview renderer is not part of the plugin. Measured output pixels are background `#09090A`, card `#101111` and primary button `#E0AE2A`. Layout and font rendering were visually compared; no pixel-identical claim is made for generated artwork/typography.

### Current runtime corrections

On 2026-10-29, the Editor and Game Development builds and Win64 Development cook/stage passed against SDK 2.21.2. All six EditorContext shell tests and the separate runtime cancellation regression passed. Both new regressions fail on the original code. A background PIE check interrupted saved-session restoration and restored the same identity on the next start. A real cooked-runtime magic-link login reached SDK connection, hidden login and released controls. The packaged acceptance check also passed a natural SDK token refresh, a successful API call three minutes later, and matching application/user identities. No Shipping validation is claimed.
