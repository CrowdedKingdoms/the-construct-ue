# AGENTS

The Construct for Unreal is a public starter project for Unreal Engine 5.8 games on Crowded Kingdoms, built on the
Crowdy SDK for Unreal. It is the Unreal counterpart of the web Construct
([CrowdedKingdoms/the-construct](https://github.com/CrowdedKingdoms/the-construct)). This GitHub repository is
published from the team's own repository: commits and pull requests made here are not merged.

## Boundaries (hard)

- **Nothing private in a tracked file.** No app ids, credentials, tokens, emails or personal paths. The app settings in
  `Config/DefaultGame.ini` are blank here; Crowdy Studio's
  [Config Sync](https://docs.crowdedkingdoms.com/unreal-sdk/studio/config-sync) writes your own app's, and they stay
  out of anything you publish.
- **Only the SDK's public surface,** used the way its docs say: the Blueprint nodes and the types and functions in
  the SDK's `Public/` folders that the [Unreal SDK docs](https://docs.crowdedkingdoms.com/unreal-sdk/installation)
  cover. Never `Private/` code, deprecated symbols, the `CrowdyCppBridge` module or the vendored CrowdyCPP
  (`crowdy/...` includes), console commands as a runtime API, or direct HTTP, WebSocket or GraphQL calls to the
  platform. If the SDK does not provide something, it does not get built around the SDK here.
- **Server truth lives in Server Objects.** Health, inventory, claims, progress and anything a cheater would change
  belong in a Server Object, which answers or refuses. CrowdyState is fast, client-authoritative view state
  (movement, animation), never the truth.
- **A third party must be able to run it cold:** clone with submodules, open the project, Config Sync your own app,
  play. Anything that needs a Crowded Kingdoms account other than your own does not belong on that path.

## The SDK submodule

- **`Plugins/CrowdySDK` is a git submodule** of [CrowdySDK-Unreal](https://github.com/CrowdedKingdoms/CrowdySDK-Unreal)
  at the commit this project pins. Clone with `git clone --recurse-submodules`, or run `git submodule update --init`
  after a plain clone; a ZIP or release archive lacks it and does not build.
- **Never edit inside `Plugins/CrowdySDK`** and never run `git submodule update --remote`: the pin is chosen for each
  branch.
- **No build input names the SDK's path.** Name the plugin `CrowdySDK` in a `.uplugin` `Plugins` list and its modules
  by name in a `Build.cs`.
- **Each branch pins the SDK of its own tier:** `dev` an SDK `dev` commit, `test` an SDK `test` commit, `prod` an SDK
  release. SDK `dev` and `test` builds dial staff-only backends by default; on `dev`, choose Production in Crowdy
  Studio before Config Sync.

## Branches

`dev`, `test` and `prod`. `prod` is the latest release, `dev` the work in progress, and changes move forward from
`dev` to `test` to `prod`. There is no `main`.

## Working in this repo

- **Build** with Unreal Engine 5.8 and Visual Studio with the Unreal workload: open `TheConstruct.uproject`, or run
  `Build.bat TheConstructEditor Win64 Development -Project="<path>\TheConstruct.uproject"` from the engine's
  `Engine\Build\BatchFiles`.
- **Code** follows Epic's Unreal C++ style. Comments explain the code to a reader who has only the code.
- **Docs:** `README.md` for setup, the
  [Unreal SDK docs](https://docs.crowdedkingdoms.com/unreal-sdk/installation), and their page for coding agents,
  [For AI agents](https://docs.crowdedkingdoms.com/unreal-sdk/for-ai-agents).
