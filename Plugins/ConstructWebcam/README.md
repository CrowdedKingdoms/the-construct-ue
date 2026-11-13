# Construct Webcam

Optional webcam video over presence pawns. Webcams are off until the project turns on **Enable Webcam** (Project Settings > Plugins > Crowdy Construct); each player then turns their own camera on, and separately chooses to see other players' cameras.

- **Camera** (`UConstructWebcamSubsystem`, a game instance subsystem): opens the first camera Windows lists through Unreal's media framework, scales each frame to 128x96, encodes it as JPEG (at most 8 KB and 10 frames a second) and sends it through the Crowdy SDK only while another player's presence pawn is within one chunk. It starts only when the player is signed in, gameplay input is allowed, the app is not paused, the player's Video Send setting allows it (players under 18 need a parent's consent) and the app's recording state is known. Its state and the reason a camera is blocked are what the UI shows; `IsBeingRecorded()` is known before the camera opens, so the recording notice comes first.
- **Feeds** (`UConstructWebcamFeeds`, a world subsystem): with Show Video on and the player's Video Receive setting allowing it, decodes nearby players' JPEG frames into one 128x96 texture per sender, drawn over that sender's presence pawn. A feed empties after 1 s without frames. WebP and malformed frames show nothing and are counted, not logged.
- **Settings:** both subsystems read the player's settings again on `RefreshSettings()`; the feeds also do so every 10 s while Show Video is on.

Capture uses the engine's WmfMedia plugin, so the camera works on Windows only. Nothing is recorded, saved or uploaded.

`SetFrameSourceForCheck`, `ClearFrameSourceForCheck` and `SetDecodedFrameObserverForCheck` exist only for the end-to-end check and are compiled out of Shipping builds.

The UI uses the real camera and feed subsystems. The temporary sample-video adapter and JPEG fixtures have been removed.
