# Construct Webcam

Optional webcam video over presence pawns. Capture, encoding and decoding are not built yet.

For now the plugin holds the types the camera toggle and the feed widgets share (camera state, the reason a camera is blocked, a feed's texture and sender), and `UStandInC9`, a temporary stand-in: a game instance subsystem that answers the camera, feed and owner-lookup calls from eight looped JPEGs under `Content/StandIn/C9/`, with fixture selectors for every camera state, block reason, owner answer and feed expiry. The owner lookup answers Confirmed, Unknown or Failed like the real call (Unknown means ask again later): by default the first ask for an actor id answers Unknown and later asks answer Confirmed. It makes no network call and holds no real state.

The stand-in exists only so the UI can be built before the real calls exist. It is deleted, with its fixtures, when the real subsystems replace it. A packaged build does not stage the loose fixture files, so the stand-in then shows no picture.
