# The Construct for Unreal

A starter project for building Unreal Engine 5.8 games on [Crowded Kingdoms](https://docs.crowdedkingdoms.com)
with the [Crowdy SDK for Unreal](https://github.com/CrowdedKingdoms/CrowdySDK-Unreal). It is the Unreal counterpart of
[the web Construct](https://github.com/CrowdedKingdoms/the-construct).

The SDK gives you the platform. This project gives you the rest of a game: sign-in, players who see each other, nearby
chat, voice and webcams, a server-held world hub built from Server Objects, a block world with a shared Paint program,
land players can claim, mods they build and run on their land, and Crowdy Studio with its agent inside the game.

Clone it, run it against your own app, then replace the demo scenes with your game.

## What you get

| Area | What it is | Where |
| --- | --- | --- |
| Sign-in | A native sign-in screen (email link, social providers, a saved session) on a startup map | `Plugins/ConstructShell/` |
| Presence | Every player as a pawn with a name tag; set your own name in game | `Plugins/ConstructPresence/` |
| Chat and voice | Nearby text chat with a channel fallback, and push-to-talk voice within one chunk | `Plugins/ConstructChat/`, `Plugins/ConstructVoice/` |
| Webcams | Opt-in camera over your pawn and other players' video, with settings and a report form | `Plugins/ConstructWebcam/`, `Plugins/ConstructPanels/` |
| World hub | Three Server Objects in Rust: a pulse every minute, the program catalog, the claim registry, each player's progress | `Content/ServerObjects/`, `Server/`, `Plugins/ConstructHub/` |
| Holodeck | The hub level: a deck with program pads, the first of which opens Paint, and claim pads | `Content/Construct/Maps/L_ConstructHolodeck` |
| Paint | A shared floor of blocks every player paints and everyone sees, kept between sessions | `Content/Construct/Maps/L_ConstructPaint`, `Plugins/ConstructWorld/` |
| Chunks | The block world drawn as instanced cubes, up to 4,096 per chunk | `Plugins/ConstructWorld/` |
| Claims and mods | Claim the chunk you stand on, then build, deploy and switch on a SERVER mod that changes your land | `Plugins/ConstructWorld/` |
| Mod Scripts | A mod's CLIENT half, run in a sandbox on the machine of each player on that land who agrees to it (Win64) | `Plugins/ConstructModScripts/` |
| Studio dock | Crowdy Studio and its agent in a panel inside the game | `Plugins/ConstructStudioDock/`, `Plugins/ConstructStudioDockCore/` |

## Getting it running

Prerequisites: Unreal Engine 5.8, Visual Studio with the Unreal workload, git, and a Crowded Kingdoms account (sign up
at [studio.crowdedkingdoms.com](https://studio.crowdedkingdoms.com)).

1. **Clone with the SDK**, into a short path (Unreal fails on very long ones):

   ```bash
   git clone --recurse-submodules --branch prod https://github.com/CrowdedKingdoms/the-construct-ue.git C:/construct
   ```

   `prod` is the latest release; `--branch dev` follows the work in progress. The Crowdy SDK arrives at
   `Plugins/CrowdySDK` as a git submodule, at the SDK commit the branch pins (on `prod`, an SDK release). If you cloned
   without it, run `git submodule update --init`. A ZIP download leaves it out and does not build.
2. **Open `TheConstruct.uproject`** and let Unreal build the modules it asks about.
3. **Point it at your own app.** The app settings in `Config/DefaultGame.ini` are blank on purpose. In the editor, open
   Crowdy Studio, sign in, create or pick an app, and use
   [Config Sync](https://docs.crowdedkingdoms.com/unreal-sdk/studio/config-sync), which writes them.
4. **Deploy the world hub.** The three Server Objects (`construct_world`, `construct_claims`, `construct_progress`)
   are defined in `Content/ServerObjects/` and their Rust code is in `Server/`. In Crowdy Studio open **Server
   Compute** and press **Deploy**, or from a shell:

   ```bash
   "C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" C:/construct/TheConstruct.uproject -run=CrowdyServerCompute -op=deploy -yes
   "C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" C:/construct/TheConstruct.uproject -run=CrowdyServerCompute -op=restart -yes
   ```

   Use your own engine and project paths.

   The platform builds the code; no Rust toolchain is needed. Run both again after you change `Server/`.
5. **Grant what the features need.** A new app's default access tier lets players move, build, chat and talk. The
   rest is opt-in, as permission keys on the tier (`updateAccessTier` in the Management API; see
   [game setup](https://docs.crowdedkingdoms.com/management-api/game-setup)):
   - `use_video_chat` for webcams;
   - `write_server_code` and `run_server_code` for SERVER mods, `write_client_code` and `run_client_code` for Mod
     Scripts;
   - `use_studio_agent` for the agent in the Studio dock.

   These are the tier's permissions, not the keys on Crowdy Studio's Access page, which are features your own server
   code checks. Grant them before anyone plays: a player who already joined keeps the permissions they joined with.
   Claiming land needs the app's claim policy at Self Claim, which is the default.
6. **Play.** Open `Content/Construct/Maps/L_ConstructHolodeck` and press Play. Sign in on the screen that appears and
   you are on the deck. Start a second instance (or package the game and run it twice) with another account to see
   each other.
   - Walk onto the **Paint** pad to open Paint, press `F4` for the pointer, pick a swatch and click the floor. The
     **Holodeck** pad takes you back.
   - Stand on a claim pad and press `E` to claim that chunk, then `F4` and **Mods** to build a SERVER mod there.
   - Press `M` for the Studio dock.

The default map, `L_ConstructStart`, is the sign-in screen on its own; the Holodeck is where the demo lives.

## Controls

| Input | Does |
| --- | --- |
| `W` `A` `S` `D` or arrows, mouse | Move and look (gamepad: sticks) |
| `Space` | Jump |
| `Enter` | Write a chat line; `F3` shows or hides the chat panel; `Escape` closes the composer |
| `F4` | Pointer mode, to click the HUD (chat channels, webcam card, Mods, the Paint palette); again to look around |
| `F2` | Set your player name |
| `V` (hold) | Push-to-talk, once **Enable push-to-talk** is ticked in the voice panel (needs Enable Voice) |
| `C` | Camera on or off; `F6` shows or hides other players' video (needs Enable Webcam) |
| `E` | Claim the chunk, or release it, on a claim pad |
| `M` | The Studio dock; `Escape` closes it |
| `F7` | The running Mod Scripts (needs Enable Mod Scripts) |

Voice, webcams and Mod Scripts are off until you turn them on in the project settings below.

## Project settings

**Project Settings > Plugins > Crowdy Construct** turns features on for the whole project. Each is read when a level
starts.

| Setting | Default |
| --- | --- |
| Enable Voice | Off |
| Enable Webcam | Off |
| Enable Mod Scripts | Off |
| Enable Studio Dock | On |

How far voice reaches is the map's **Voice Distance** in the Crowdy SDK's map profile, 1 chunk by default.

## How it is put together

- `Source/`: the game module, kept small; the features live in the plugins.
- `Plugins/Construct*`: one plugin per feature, each using only the Crowdy SDK's public nodes and C++ API.
  `Plugins/ConstructHarness` holds the unattended checks the team runs on packaged builds.
- `Plugins/CrowdySDK`: the Crowdy SDK for Unreal, a git submodule. Never edit inside it; update it by moving the
  submodule.
- `Content/ServerObjects/` and `Server/`: the world hub's Server Object definitions and their Rust code. The SDK's
  Generate Server Code writes `lib.rs` and `types.rs` from the definitions; `logic.rs` is yours.
- `Content/Construct/`: the maps, materials and Blueprints.
- `AGENTS.md`: the rules for anyone, person or coding agent, building on this starter.

The [Unreal SDK docs](https://docs.crowdedkingdoms.com/unreal-sdk/installation) cover everything the SDK provides.

## Branches and versions

`prod` is the latest release, `test` the next one and `dev` the work in progress. Each pins the Crowdy SDK of the same
tier, and `prod` pins an SDK release. The project's version is `ProjectVersion` in `Config/DefaultGame.ini`: `X.Y.Z` on
`prod`, `X.Y.Z-test.N` on `test` and `X.Y.Z-dev.N` on `dev`.

This repository is published from the team's own repository, so pull requests made here are not merged. The MIT
license is in `LICENSE`.
