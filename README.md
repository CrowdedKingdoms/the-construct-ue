# The Construct for Unreal

A starter project for Unreal Engine 5.8 games on [Crowded Kingdoms](https://docs.crowdedkingdoms.com), the Unreal
counterpart of [the web Construct](https://github.com/CrowdedKingdoms/the-construct). It will show, one small working
example at a time, how to do the mechanics a Crowded Kingdoms game needs: sign-in, players seeing each other, chat and
voice, server-held truth with Server Objects, a block world, claiming land, and player-made mods.

**Status:** an empty project with the Crowdy SDK attached. No feature has been built yet.

## Getting set up

Prerequisites: Unreal Engine 5.8, Visual Studio with the Unreal workload, and git.

1. Clone with the SDK submodule, into a short path: `git clone --recurse-submodules --branch prod
   https://github.com/CrowdedKingdoms/the-construct-ue.git <path>`. `prod` is the latest release and `dev` follows
   the work in progress. Until the first release, `prod` holds no project yet: clone `dev`, which dials staff-only
   backends by default, and choose Production in Crowdy Studio in step 3. The Crowdy SDK arrives at
   `Plugins/CrowdySDK`, from [CrowdySDK-Unreal](https://github.com/CrowdedKingdoms/CrowdySDK-Unreal) at the commit
   this project pins. If you cloned without it, run `git submodule update --init`; a ZIP download lacks it and does not
   build.
2. Open `TheConstruct.uproject` and let Unreal build the modules it asks about.
3. Point the project at your own app. The app settings in `Config/DefaultGame.ini` are blank here: sign in to Crowdy
   Studio in the editor and use [Config Sync](https://docs.crowdedkingdoms.com/unreal-sdk/studio/config-sync), which
   writes them.

## What is here

- `Source/`, `Config/` and `TheConstruct.uproject`: the template project; `Content/` arrives with its first assets.
- `Plugins/CrowdySDK`: the Crowdy SDK for Unreal, a git submodule. Never edit inside it.
- `AGENTS.md`: the rules for anyone, person or coding agent, building on this starter.
- `LICENSE`: the MIT license.

## Branches and versions

`dev`, `test` and `prod`, each pinned to the SDK of the same tier; `prod` pins an SDK release. The starter's version is
`ProjectVersion` in `Config/DefaultGame.ini`, written `X.Y.Z-dev.N` on `dev`, `X.Y.Z-test.N` on `test` and `X.Y.Z` on
`prod`.

This repository is published from the team's own repository, so commits and pull requests made here are not merged.
The [Unreal SDK docs](https://docs.crowdedkingdoms.com/unreal-sdk/installation) cover everything the SDK provides.
