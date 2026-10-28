# The Construct for Unreal

A starter toolkit for Unreal Engine 5.8 games on [Crowded Kingdoms](https://docs.crowdedkingdoms.com), the Unreal
counterpart of [the web Construct](https://github.com/CrowdedKingdoms/the-construct). It will show, one small working
example at a time, how to do the mechanics a Crowded Kingdoms game needs: sign-in, players seeing each other, chat and
voice, server-held truth with Server Objects, a block world, claiming land, and player-made mods.

**Status:** an empty project with the Crowdy SDK mounted. No feature has been built yet. The project lives in a private
Diversion repo and is published, one way, to GitHub at
[CrowdedKingdoms/the-construct-ue](https://github.com/CrowdedKingdoms/the-construct-ue).

## Getting set up from GitHub

Prerequisites: Unreal Engine 5.8, Visual Studio with the Unreal workload, and git.

1. Clone with the SDK submodule, into a short path: `git clone --recurse-submodules --branch prod
   https://github.com/CrowdedKingdoms/the-construct-ue.git <path>`. `prod` is the latest release and `dev` follows
   the work in progress. Until the first release, `prod` holds no project yet: clone `dev`, which dials staff-only
   backends by default, and choose Production in Crowdy Studio in step 3. The Crowdy SDK arrives at
   `Plugins/CrowdySDK`, from
   [CrowdySDK-Unreal](https://github.com/CrowdedKingdoms/CrowdySDK-Unreal) at the commit this project pins; no
   Diversion invite is needed. If you cloned without it, run `git submodule update --init`.
2. Open `TheConstruct.uproject` and let Unreal build the modules it asks about.
3. Point the project at your own app. The app settings in `Config/DefaultGame.ini` are blank in the published copy:
   sign in to Crowdy Studio in the editor and use
   [Config Sync](https://docs.crowdedkingdoms.com/unreal-sdk/studio/config-sync), which writes them.

The GitHub repo is a published copy: changes are made on Diversion and published, so it does not take pull requests.
The Diversion steps below (`dv`, `scripts/`, `.dvhooks/`) do not apply to a GitHub clone.

## Getting set up on Diversion

Prerequisites: Unreal Engine 5.8, Visual Studio with the Unreal workload, the Diversion CLI (`dv`), and access to both
this repo and the `crowdy-sdk` repo on Diversion. The repo owner invites you with Write on this repo and Read on
`crowdy-sdk` (two separate invites).

1. Accept both invites from their emails or the Diversion web app, then sign in to the CLI with `dv login`. If the
   `crowdy-sdk` access is still missing later, `scripts\sync-sdk.ps1` says so and prints the
   `dv invite accept <repo id>` command to run.
2. Clone from a folder that is not inside another Diversion workspace (a clone started inside one fails with "either
   provide an existing workspace, or set force-new-workspace, but not both"):
   `dv clone the-construct-ue <path> --new-workspace --ref dev`, then `cd <path>`. Use a short path. The clone does
   not include the SDK yet; the next step fetches it.
3. From PowerShell (never Git Bash), fetch the SDK mount at its pinned commit and build; the first run takes about 15
   minutes: `powershell -ExecutionPolicy Bypass -File scripts\sync-sdk.ps1 -Build`
4. Revision control in the editor, once per machine: install the Diversion plugin for UE 5.8 (from Fab, then Epic Games
   Launcher > Library > Install to Engine, or the UE 5.8 source zip listed at docs.diversion.dev). The project already
   enables it as an optional plugin, so do not enable it again in Edit > Plugins. When the editor opens in step 7, use
   Revision Control > Connect to Revision Control > Diversion > Accept Settings. Never click "Initialize project with
   Diversion": this folder is already a repository. The plugin
   cannot switch branches: switch with `dv`, then reload open assets. Do not change the plugin's soft-lock warning
   setting: it writes a project config file that affects everyone.
5. Before you save anything in the editor, cut your work branch:
   `powershell -ExecutionPolicy Bypass -File scripts\new-branch.ps1 -Name <category>/<name>` (AGENTS.md 4.2). The
   clone lands on `dev`, and nobody commits on `dev`.
6. Agent setup, once per machine. Every agent reads `AGENTS.md`: Claude Code through `CLAUDE.md`, Gemini CLI through
   `GEMINI.md`, and Codex, Grok Build, Cursor, Copilot and most others directly. Always start the agent from the repo
   root.
   - Codex finds the project root by `.git`, which Diversion does not have, so it reads only the folder it starts in.
     Optional: set `project_root_markers = [".git", ".diversion"]` in `~/.codex/config.toml` (keep `.git`: the key
     replaces the default for every project). Trust the project when Codex asks, so `.codex/rules/dv.rules` loads; if
     it does not ask, its startup message names the key to add. Expect its sandbox to need your approval for `dv`
     commands, which reach the Diversion agent or the network (untested here). Its rules match a fixed command prefix,
     so they miss a guarded flag later in the line (`dv merge <branch> --into dev`); there `AGENTS.md` section 6 binds.
     Never give Codex a blanket allow rule for `dv`.
   - Antigravity cuts a rule file at 24,000 bytes, inside `AGENTS.md` section 6: start each session by telling it to
     read `AGENTS.md` in full with its file tool.
   - Claude Code and Grok Build pick up `.claude/settings.json`; Grok Build needs folder trust first.
   - Other agents have no guard file here, so `AGENTS.md` alone binds them. Check yours has loaded it before it starts.
7. Open `TheConstruct.uproject`.

Run `powershell -ExecutionPolicy Bypass -File scripts\check-sdk-pin.ps1` any time to confirm the SDK mount is pinned,
clean and not on a branch. The scripts work in Windows PowerShell 5.1 and PowerShell 7; `-ExecutionPolicy Bypass` is
needed because Windows blocks unsigned scripts by default.

## A second agent at the same time

One folder, one branch, one agent. For a second agent, clone another workspace from a folder outside any Diversion
workspace and not in a cloud-synced or network folder: `dv clone the-construct-ue <path> --new-workspace --ref dev`.
Run `scripts\sync-sdk.ps1 -Build` there, then cut its own branch with `scripts\new-branch.ps1`. When it is done, commit
or hand over its work, run `dv unregister -f` inside that folder, then `dv workspace delete <id> -f` (ids from
`dv workspace`), then delete the folder. An agent asks its developer before either `dv` command.

Optional, skips about 130 MB of unused test files: the SDK mount is the whole `crowdy-sdk` repo, whose root is the SDK's
own test project, and Unreal only loads the plugin. In the Diversion web UI, use selective sync on your "Crowdy
submodule" workspace to untick the mount's `Config`, `Content` and `Source` folders (the loose `CrowdySandbox.uproject`
stays; it is harmless).

## How this repo works

- `AGENTS.md`: the rules for every developer and every AI agent. Read it before your first change.
- `CHANGELOG.md`: what changed, newest first, with the SDK pin on every entry.
- `Changes/`: one CHANGELOG fragment per branch that changes something users see, `Changes/<branch>.md` with `/` written as `-`. Folded into
  `CHANGELOG.md` at a release.
- `docs/`: user-facing documentation, added as features land.
- `Internal/`: the plan, the contract record, the SDK gaps, dated facts, as-built notes and the AI review brief. Never
  published.
- `.dvhooks/`: the pre-commit hook that `dv commit` runs.
- `scripts/`: `sync-sdk` (fetch and build the SDK mount), `check-sdk-pin` (verify the pin), `new-branch` (cut a branch),
  `check-branch` (the check before landing: merges `dev` in, runs the checks, prints the note for the as-built record).
- `Plugins/Crowdy`: the Crowdy SDK, mounted from the `crowdy-sdk` repo at a pinned commit. Never edit inside it. On
  GitHub the SDK is at `Plugins/CrowdySDK` instead, a git submodule of CrowdySDK-Unreal.
- `LICENSE`: the MIT license.

## Versions

Two version lines, as in the web Construct: the starter (this template project, `ProjectVersion` in
`Config/DefaultGame.ini`) and the framework (the Construct plugins' `VersionName`, once the first one exists). Each is
written `X.Y.Z-dev.N` on `dev`, `X.Y.Z-test.N` on `test`, and `X.Y.Z` on `prod`.
