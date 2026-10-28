# AGENTS

Rules for every developer and AI coding agent on the Unreal Construct, on Diversion or from the GitHub mirror. Read the whole file before starting work. This file stays at or under 32,000 bytes so Codex loads all of it; the pre-commit hook refuses more. Some agents cut a rule file sooner: if you cannot see section 11, read this file in full with your file tool. Write paths in backticks, never as `@path`.

**C0, the working agreement: Set v2 (repo owner, 2026-10-27).** The pre-work setup here and in `Internal/Contracts.md` C0 is the repo owner's decision alone; the UE developer joins after the invite. Gates C1 to C12 still need both developers (section 3).

**Read before acting** (`Internal/` is never mirrored to GitHub):

| Task | Read |
| --- | --- |
| Release, promotion, version bump, tag, folding `Changes/` into `CHANGELOG.md` | `Internal/Owner/Releases.md` |
| Repin, first mount, a pin's tier, the GitHub mirror, publishing | `Internal/Owner/SDK-Pin-And-Mirror.md` |
| Filing an SDK gap | `Internal/SDK-Gaps/README.md` |
| An as-built note | `Internal/As-Built/README.md` |
| A contract gate | `Internal/Contracts.md` |
| The AI review | `Internal/Harness/Review.md` |
| Plan and layer status | `Internal/Plan.md` |

## What this repo is

A light public starter toolkit (Construct plugins plus a thin template project) that mirrors the web Construct (github.com/CrowdedKingdoms/the-construct) on the Crowdy SDK for Unreal. Diversion is the source of truth; the GitHub repo is a one-way mirror that nobody edits.

- **The repo owner:** the repo, every SDK change, the lower-level track (Rust hub logic, the deploy pipeline, harnesses, checks, setup and smoke scripts) and the owner-only actions (4.4).
- **The UE developer:** the UE track, everything players see, on the SDK's Blueprint nodes; also the hub's Server Object definitions, paired with the repo owner, who writes the Rust behind them.
- **Peers:** each path has a default area (`Internal/Contracts.md` C0.1), but either developer may change anything except owner-only actions and the AI harness (4.4), flagging first (section 7). Neither approves the other's work. Contact is Discord: answer within one working day, or say when.

**Agents:**
- **`AGENTS.md` is the only rules file.** `CLAUDE.md` (`@AGENTS.md`) and `GEMINI.md` (`@./AGENTS.md`) only load it; other agents read it directly, Claude Code too when no `CLAUDE.md` exists (keep `CLAUDE.md` anyway). Confirm your agent has loaded it.
- **Start your agent from the repo root:** Diversion has no `.git`, so some agents (Codex among them) read instructions only in the folder they start in. Per-agent setup: `README.md`.
- **Never add another instruction file** (`AGENTS.override.md`, `AGENT.md`, a nested `AGENTS.md`, `.cursorrules`, `.github/copilot-instructions.md`): some agents would read it instead of this one.
- **Guard files are a net:** `.claude/settings.json` (Claude Code, Grok Build) and `.codex/rules/dv.rules` (Codex) stop some `dv` commands (section 6). This file binds every agent.
- **The mount's own `CLAUDE.md`, `README.md` and `scripts/`** govern crowdy-sdk work, not this repo; this file wins. Never run a script from the mount.

### If you cloned this from GitHub

- **This copy is generated from Diversion;** GitHub commits and pull requests are not merged.
- **Clone from `prod` with the SDK:** `git clone --recurse-submodules -b prod <repo URL>` (or `git submodule update --init` after a plain clone); a ZIP or release archive lacks `Plugins/CrowdySDK` and does not build. `dev` and `test` dial staff-only backends.
- **Never edit inside `Plugins/CrowdySDK`** or run `git submodule update --remote`. Your app id and credentials never go in a tracked file.
- **`Internal/`, `dv` and `scripts/` are Diversion-only;** sections 3, 8 and 9 do not gate your own game.

## 1. Use the SDK, never a parallel path

1. **Use the SDK's exposed surface, the way its docs say, for anything it provides:** sign-in, app tokens, CrowdyState, CrowdyEvent RPC, channels, voice, webcam receive, the exec connection, Server Objects, the definition asset, Generate, the `CrowdyServerCompute` and `CrowdyStudio` commandlets, settings, Config Sync.
2. **Exposed** means, at the pinned SDK commit:
   - an entry in one of the ten Blueprint groups of the surface manifest (`/helpers/unreal-sdk/sdk-surface.json` on the pin's tier's docs site) with `"public_header": true`, no `DeprecatedFunction` in its `meta`, and, for runtime code, `"editor_module": false`
   - a type, function or reflected property in an SDK `Public/` folder that the tier's docs cover (C++ calls, and definition fields set from editor Python)
   - a commandlet op the docs present, such as `-run=CrowdyServerCompute -op=deploy` (never include, subclass or call the commandlet class)

   If the manifest's `sdk_version` predates the pin, the `Public/` headers under `Plugins/Crowdy/Plugins/CrowdySDK/Source/` and the tier's docs are the truth. A `Public/` symbol no docs page covers is not exposed yet: ask the repo owner.
3. **Never use:** `Private/` code or `"public_header": false` entries; deprecated symbols; `CrowdyCppBridge` (its `Public/` headers too) or the vendored CrowdyCPP (`crowdy/...` includes); the raw `cpp_*` groups, except the Blueprint enums `ECrowdyMessageType` and `ECrowdyErrorCode`; editor-only modules from runtime code; test hooks; console commands as a runtime API; direct GraphQL, HTTP or socket calls to the platform.
4. **Never invent a second way:** no copy, no workaround, no "until the SDK has it", no script doing by hand what a documented SDK command does. If the SDK piece is not ready, use stand-in data and the agreed names, or the layer is deferred.
5. **Parallel-path markers are a stop:** `CrowdyCppBridge` in a Construct `Build.cs`; a `crowdy/` include; an include or include path into an SDK `Private/` folder or naming `Plugins/Crowdy`; `FHttpModule` or `IWebSocket` in Construct code; a Construct class copying or shadowing an SDK class or node; any edit in `Plugins/Crowdy`; authoritative state in CrowdyState.
6. **Authoritative or cheat-sensitive state** (HP, inventory, claims, progress) lives in a Server Object, never in CrowdyState.
7. **The layer 4 fallback** (the JS hub through the raw exec call in `CrowdyCppBridge`) is no way around this section: its preconditions are in `Internal/Plan.md`, and until all of them hold, layer 4 is deferred.

### Flagging an SDK gap

1. **Stop that piece.** Build no workaround.
2. **Confirm it is a gap** against the manifest, the pinned `Public/` headers and the docs, then ask the repo owner whether SDK `dev` has it; if so, it is a repin request.
3. **Write one file,** `Internal/SDK-Gaps/<yyyy-mm-dd>-<short-name>.md`, in the format in `Internal/SDK-Gaps/README.md`.
4. **Report it to the repo owner the same day:** an agent drafts the entry and the message, the developer sends them. The repo owner registers it and decides: an SDK slice, a deferral, or Construct-only with the reason written down.
5. **Keep building** on the agreed contract with stand-in data. A gap that changes a contract returns that gate to Proposed.
6. **After the SDK merge,** the repo owner repins, the UE track swaps in the real node, and the entry closes with the commit.

## 2. Construct-only work (fine to build here)

**The test:** would a game that is not the Construct need it? Yes: the SDK. No: here. Not sure: ask the repo owner. Construct-only:
- **Shell:** GameInstance, GameMode, PlayerController, the HUD on `ACrowdyHUDBase`, Enhanced Input, the sign-in screen.
- **Content and UI:** Holodeck, pads, program levels, primitive pawns, materials, the instanced-cube renderer, palette, block table, Paint, chat, push-to-talk, claim pad and box, mod panel, webcam toggle, consent prompt, dock placement.
- **Hub:** its Server Object types, made the SDK's documented way (C4 decides which), each type's `logic.rs`, its deploy (4.11).
- **Starter choices:** offered host calls (on the SDK's registration API), the game data the agent observes, the per-voxel state format, the axis mapping (C5).
- **Tooling:** setup, smoke, harness, publish and pin-check scripts.
- **Composition:** a helper calling SDK nodes in this starter's order; one any game would want is an SDK candidate.

## 3. Contract gates

- **Gates C0 to C12** fix the interfaces between the two tracks (names, signatures, data shapes, events, errors, stand-in data): C0 the working agreement, C1 to C11 one per layer, C12 the SDK gap format from layer 4 on. The record is `Internal/Contracts.md`.
- **C0 is Set v2 (repo owner, 2026-10-27).** C1 to C12 need both developers.
- **Build** on a gate C1 to C12 only when its Status is `Agreed vN`, both "Agreed by" lines carry a name, and the Date is set.
- **Otherwise stop:** draft a proposal into the gate as `Proposed vN` and ask both developers. One yes is not both. Record a developer's yes only when they gave it in this session; whoever records the second yes also sets `Agreed vN` and the Date.
- **Changing an agreed contract** (a renamed node, a new field, a different event) returns the gate to `Proposed v(N+1)`.
- **From C2 on, a gate lists who builds what.** Build only what it gives your developer; an item on the other list needs a handover and a new version.
- **The UE developer never waits on an SDK feature branch:** build on the agreed names with stand-in data, and switch once the real node is on SDK `dev` and this repo is repinned.

## 4. Branches and merges

### 4.1 Tier branches

- **`dev`, `test` and `prod`,** each pinned to the same-named SDK tier, are never committed to directly, deleted, renamed, or used as the first segment of another name (`dev/x` is wrong; `promote/dev-to-test-...` is fine). There is no `main`; never create one.
- **Until the first promotion** nobody works on `test` or `prod`; their rules are older than `dev`'s, so read `dev`'s.
- **Protection** is set by the repo owner in the web UI (`Internal/Owner/Releases.md`), and `Internal/Facts.md` records when it was tested. Until a test is recorded, nothing on the server refuses a direct commit: your own `dv branch-name` check before every commit and the hook (4.10) are the guard.

### 4.2 Work branches

- **One branch per work item,** cut from an up-to-date `dev` with `powershell -ExecutionPolicy Bypass -File scripts\new-branch.ps1 -Name <category>/<name>`. For changes made on `dev` by mistake, add `-CarryChanges`. By hand: no pending changes, then `dv checkout dev --ignore-shelf`, `dv update`, `dv branch -c <name>`.
- **Names:** `<category>/<short-kebab-name>`, lowercase, one to four words, with no dates, personal names, ticket or phase ids. Categories: `feature/`, `fix/`, `chore/` (`chore/harness-<name>` for a harness change) and `docs/`. The repo owner's: `repin/sdk-<tier>-<N>`, `repin/sdk-check-<name>`, `promote/dev-to-test-YYYYMMDD` or `promote/test-to-prod-YYYYMMDD` and `chore/release-<starter|construct>-<version>`. crowdy-sdk's categories do not apply here.
- **Pending files are always questioned.** At the start of a session, before cutting or switching branches, before landing and before ending a session, run `dv status` and ask your developer about every uncommitted, new or deleted file: commit it, keep it for later, or discard it. Keeping means shelving: `dv shelf create <name> <paths>` clears them from the workspace, `dv shelf apply <name>` brings them back; the scripts and `dv merge` need a clean workspace. Never silently leave, commit or discard pending work: Diversion syncs files to the cloud, but nothing is in the repo until it is committed and merged.
- **Stay current.** No time limit, but merge `dev` in at the start of each session on the branch and again before landing (`scripts\check-branch.ps1` does that one). By hand: commit, `dv merge dev`, `dv submodule status`, `dv conflict-backups list`. Fix any conflict or issue on the branch, then rebuild and re-verify. If `AGENTS.md` or `scripts/` changed, re-read `AGENTS.md`. Never cherry-pick between tiers. Every fix, urgent or not, is cut from `dev` and promoted like any change.
- **Conflicts are settled on your branch, never on a tier:** stop, give your developer the file list from `dv status`, and wait. They resolve text conflicts in the Diversion app; binaries (4.12) and `revisions.json` (4.11) are never hand-merged.
- **Lost-work check:** after any `dv merge`, `dv update` or `dv checkout`, and before every commit, run `dv conflict-backups list`. A listed file is your edit, moved to a `.dv-conflict` copy while the incoming version took its path: stop, do not commit, tell your developer. They recover the edit and run `dv conflict-backups clear <path>` in their own terminal; an agent never clears backups.
- **A branch belongs to whoever cut it.** Never check out, commit to, merge from or delete another developer's branch unless they handed it over in this session; if unsure whose it is, ask your developer. If `dv branch -c` says the name exists, pick another; never resume an old branch.
- **One folder, one branch, one agent:** never two agents, or a person and an agent, in one workspace folder at once. A second concurrent agent gets its own clone (`README.md`). No workspace in a cloud-synced or network folder.
- **Close the Unreal editor before `dv checkout`, `dv merge` or `dv update`;** if it was open, reload open assets (Asset Actions > Reload) before editing them.

### 4.3 Landing work on `dev`

The author lands their own branch. There is no review or approval step.

1. **Commit by name:** `dv commit <paths> -m "<message>"`, naming only the files you meant to change. Never `dv commit -a`: it also takes editor re-saves, other sessions' files and Config Sync output. Undo only your own changes (`dv reset <path> -f`, an `ask` line; without `-f` it asks a question). Messages: 4.9.
2. **Check:** run `powershell -ExecutionPolicy Bypass -File scripts\check-branch.ps1` (it merges `dev` in first and builds what the change needs) and the before-merge checklist (section 8).
3. **Record** the verification in the feature's as-built note (section 9).
4. **Merge:** `dv merge <branch> --into dev`, an `ask` line: an agent runs it only on a yes its developer gives after seeing check-branch's report and the AI review; if the guard blocks it, give your developer the commands check-branch printed, never retry. Its subject, `Merge branch <branch> into dev`, goes public. A conflict means `dev` moved after your checks: stop, tell your developer, redo step 2. Never resolve a conflict on `dev`.
5. **Leave the branch:** close the editor, then `dv checkout dev` and `dv update`. A merged branch takes no more commits; start the next item with `scripts\new-branch.ps1`.

Merges into `test` and `prod` are the repo owner's.

### 4.4 Who changes what

- **The AI harness:** `AGENTS.md`, `CLAUDE.md`, `GEMINI.md`, `.claude/settings.json`, `.codex/rules/`, `.dvhooks/`, `Internal/Harness/` and C0 in `Internal/Contracts.md`. Either developer proposes a change on a `chore/harness-<name>` branch. It merges only after both developers say yes in Discord, and its as-built note (category Tooling) records the date of both yeses. Before the invite, the repo owner decides alone.
- **Owner-only actions** (they need the owner's access, or they are releases): repin and first mount, promotion, version bumps and tags, folding `Changes/` into `CHANGELOG.md`, the GitHub mirror and publishing, branch protection, and invites.
- **Everything else,** infra included (`scripts/`, `.dvignore`, `Config/`, `Build/`, `Tools/`, the `Server/` crates, `Internal/Plan.md`, `Internal/Facts.md`): either developer changes it and merges after the checklist, flagging first (section 7) when it touches the other developer's area or something they build on.

### 4.5 Promotion

The repo owner's only, not before the Construct leaves `dev`: `Internal/Owner/Releases.md`.

### 4.6 Versions and tags

Versions read `X.Y.Z-dev.N` on `dev`, `X.Y.Z-test.N` on `test` and `X.Y.Z` on `prod`. Bumps and tags are the repo owner's: `Internal/Owner/Releases.md`.

### 4.7 CHANGELOG

- **A work branch never edits `CHANGELOG.md`.** A user-visible change or a pin move adds `Changes/<branch name with / replaced by ->.md` (such as `Changes/feature-presence-pawn.md`) in the same change; the repo owner folds it in.
- **The entry is prose, opening with** the SDK pin (after the word "repin" for a pin move): crowdy-sdk `dv.commit.N` and the SDK version (`2.21.0` for a release, `dev after 2.21.0` for a `dev` pin). Then a short list of what changed. The repo owner adds the version line when folding. A repin entry gives a Construct action for each SDK item ("none required" included).

### 4.8 The GitHub mirror

Only the repo owner's publish script writes it; nobody commits, opens a PR, merges or tags there by hand. How it works: `Internal/Owner/SDK-Pin-And-Mirror.md`.

### 4.9 Text that goes public

The mirror publishes each tier commit's subject (so every `Merge branch <your branch> into dev`), the name and email of whoever merged, the dates, and every tracked file not excluded, `Changes/` included. Work-branch commit messages stay on Diversion. So:
- **Branch names** stay clean (4.2).
- **Commit messages:** a short subject, a blank line, a few bullets. Never AI attribution or co-author lines, ticket, task, phase or note ids, app ids, hostnames, emails or tokens.
- **Code comments** explain the code to a reader who has only the code; never cite a plan, phase, ticket or note.
- **No em-dash characters** in code, comments, docs or commit text.

### 4.10 The pre-commit hook

Commits from the `dv` CLI or the Diversion desktop app run `.dvhooks/pre-commit.ps1`; a committed folder is checked file by file.

- **It refuses a commit** on a tier or on a branch whose name breaks 4.2; with AI attribution or an em-dash in the message; that adds a `.gitignore` or `.gitmodules`, an em-dash in a text file, or a private key; that carries an `AGENTS.md` over 32,000 bytes; or that leaves `.claude/settings.json` or `.codex/rules/dv.rules` different from the section 6 block.
- **It notes, without refusing,** a harness path (4.4), and `Plugins/Crowdy` outside a `repin/` or `promote/` branch.
- **Limits:** a net, not a guard. `--no-verify` skips it; web or API commits and server-side merges never run it; the Unreal plugin's Submit Content is untested; a branch has it only once it contains `.dvhooks/`.

### 4.11 Server deploys

- **Either developer may deploy,** only from a workspace where `dev` was merged in this session: one deploy replaces all of the app's server code, so a stale tree removes the other developer's types.
- **Commit the `revisions.json` the deploy writes** with the change, and record the deploy in the feature's as-built note under "Deploys" (date, app, types, what changed).
- **A `revisions.json` conflict is never hand-merged:** take `dev`'s copy and redeploy from the merged tree.
- **Announce each deploy in Discord** as you make it: date, app, types, branch. Before deploying, read the other developer's latest announcement: a type they deployed from a branch not yet on `dev` is removed by your deploy, so agree the order with them first.

### 4.12 Unreal assets and levels

- **Both developers create and edit levels.** Before a level's first commit, turn on One File Per Actor (World Partition, or World Settings > Use External Actors), so two people can edit it at once. Commit its `.umap` with its external actor files (name their folder: the CLI cannot take hundreds of paths).
- **A binary conflict** (any `.uasset` or `.umap`, Blueprints included) **is never hand-merged.** The other side's author picks the whole copy that wins, and the lost edit is redone.
- **The Diversion editor plugin's padlocks** show what the other developer is editing.
- **Renaming or moving an asset already on `dev`** goes on its own small branch, flagged in Discord, with Fix Up Redirectors run and every resaved file committed, merged the same day.

## 5. The SDK submodule

### 5.1 Diversion: `Plugins/Crowdy`

crowdy-sdk is mounted at `Plugins/Crowdy`, and Unreal finds `Plugins/Crowdy/Plugins/CrowdySDK/CrowdySDK.uplugin` there by itself.

1. **Never edit, save or commit anything under `Plugins/Crowdy`.** A save there is silent, blocks the next repin, and never shows in this repo's diff. Check with `dv status` run inside it, and ask before discarding. A change the SDK needs is a gap.
2. **The mount sits detached at the pin:** `dv submodule status` reads `synced` with 0 local changes (`scripts\check-sdk-pin.ps1` checks it). `pin_mismatch` right after a sync, merge or checkout: run rule 3; if it remains, or the mount is dirty, tell the repo owner. "Repo ... not found" means no access to crowdy-sdk: tell the repo owner.
3. **After anything that moves the pin,** close the editor and IDE and run `powershell -ExecutionPolicy Bypass -File scripts\sync-sdk.ps1 -Build` from the repo root: it re-syncs the mount, clears stale build output and builds the editor.
4. **Only the repo owner repins,** on a `repin/sdk-<tier>-<N>` or promote branch: `Internal/Owner/SDK-Pin-And-Mirror.md`.
5. **Feasibility pins** (only on `repin/sdk-check-<name>`) are the repo owner's: same file.
6. **The first mount:** the repo owner's, same file.

### 5.2 GitHub: `Plugins/CrowdySDK`

The mirror carries CrowdySDK-Unreal as a git submodule at `Plugins/CrowdySDK`, written by the publish script. The two paths differ, so no build input (`Source/`, `Config/`, `Content/`, a Construct plugin, the `.uproject`) names `Plugins/Crowdy` or `Plugins/CrowdySDK`: name the plugin `CrowdySDK` in a `.uplugin` `Plugins` list, and modules by name in `Build.cs`. Docs, `scripts/` and `Internal/` may name the mount.

### 5.3 Translating the pin

The publish script's job: `Internal/Owner/SDK-Pin-And-Mirror.md`.

### 5.4 Refuse unless public

The publish script refuses a pin with no public SDK commit, so the GitHub Construct may lag Diversion: same file.

## 6. Never

1. Commit directly on `dev`, `test` or `prod`; create `main`; or delete or rename any branch (the repo owner prunes merged ones).
2. Edit under `Plugins/Crowdy` or inside `Plugins/CrowdySDK`, or change the SDK pin unless you are the repo owner on a `repin/` or `promote/` branch.
3. Call past the SDK surface, copy SDK code, or work around a missing SDK feature.
4. Put authoritative or cheat-sensitive state in CrowdyState.
5. Build against a gate C1 to C12 that is not Agreed, or record a yes (or `Agreed`) that a developer did not give in this session.
6. Take an owner-only action unless you are the repo owner: repin or first mount, promote, bump a version or tag, fold `Changes/` into `CHANGELOG.md`, publish (the Construct or the SDK mirror), change branch protection, or send invites.
7. As an agent, merge into a tier without a yes your developer gave after check-branch's report and the AI review (4.3).
8. Run a guarded dv command except as the block below allows (a `--conflict_resolution` strategy can drop the other side's hunks while the build stays green).
9. Touch the GitHub mirror by hand: commits, PRs, merges, tags or rulesets.
10. Commit anything private: tokens, passwords, emails, personal paths, marketplace content. The shared app's keys (`AppID`, `OrgId`, `Environment`, `DiscoveryUrl`, `GameApiHttpUrl`, `GameApiWsUrl` under `[/Script/CrowdyReplication.CrowdySDKDeveloperSettings]`) are not secrets: Config Sync writes them into `Config/DefaultGame.ini` and they are committed there, never typed by hand or copied anywhere else. The publish script blanks them in the GitHub mirror. Until an Agreed C1 names the app, nobody here runs Config Sync, `-op=sync` or the Studio Connection tab.
11. Add a `.gitignore` on Diversion; ignore rules go in `.dvignore`.
12. Hand-edit `Cargo.toml`, `src/lib.rs` or `src/types.rs` under `Server/`; Generate owns them.
13. Follow the mount's `CLAUDE.md` or `README.md`, or run its scripts.
14. Add AI attribution or co-author lines to a commit.

### Guarded dv commands

An agent never runs a `deny` line, and runs an `ask` line only after its developer says yes in this session (`*` is any text). Call `dv` by its bare name, never as `dv.exe`, by a full path or through another command. This block is the only list: `.claude/settings.json` and `.codex/rules/dv.rules` carry it, and the pre-commit hook refuses a commit where they differ. Change it here and in both files in one harness change (4.4). Your developer, not an agent, may run `dv conflict-backups clear` after recovering their edit (4.2).

```text
deny dv repo -d *
deny dv obliterate *
deny dv prune *
deny dv branch -d *
deny dv branch -r *
deny dv tag -d *
deny dv tag -m *
deny dv commit *--no-verify*
deny dv merge *--delete-branch*
deny dv conflict-backups clear *
deny dv lock -d -f *
deny dv lock -d * -f
ask dv merge *--into*
ask dv merge *--conflict_resolution*
ask dv update *--conflict_resolution*
ask dv revert *--conflict_resolution*
ask dv checkout *--discard-changes*
ask dv reset *
ask dv clean *
ask dv revert-to-commit *
ask dv workspace *delete*
ask dv unregister *
ask dv submodule repin *
ask dv submodule add *
ask dv submodule rm *
ask dv tag -c *
```

## 7. Stop and ask

| Situation | Ask |
| --- | --- |
| A gate C1 to C12 is not Agreed, or a harness change (4.4) | Both developers |
| The change touches the other developer's area (C0.1), a shared file, a tool, script or API they build on, a contract, or a deployed Server Object type | Your developer, who flags it in Discord (no approval needed); continue once they say the flag went out |
| `dv status` shows an uncommitted, new or deleted file | Your developer: commit it, keep it for later, or discard it |
| An owner-only action; a missing or undocumented SDK feature; SDK versus Construct unclear | Repo owner |
| A hand edit to a key Config Sync or the Studio console writes | Repo owner |
| A merge or update conflict, or `dv conflict-backups list` shows a file | Your developer |
| A conflict in a binary file (4.12), the pin, or several hunks of one file | The author of the other side |
| This file disagrees with the plan or a gate C1 to C12 | Both developers; the gate wins until changed |

## 8. Checklists

### Pre-flight (start of a session)

1. **Read** this file, the task's plan section and status row, its gate, and the as-built notes in its category. Your agent started in the repo root.
2. **Pending files:** run `dv status` and ask about each (4.2); `dv conflict-backups list` is empty.
3. **Branch:** one you cut for this task, or one handed to you in this session; never a tier or a branch already merged into `dev`. Otherwise cut one with `scripts\new-branch.ps1`.
4. **Current:** merge `dev` in (4.2).
5. **Mount:** `scripts\check-sdk-pin.ps1` passes.
6. **Gate:** the task's gate (C1 to C12; C0 is Set) reads `Agreed vN` with both names and a date; if not, stop, draft, and ask both developers.
7. **Flag** before the first edit that touches the other developer, not before the merge (section 7).
8. **SDK and scope:** each SDK feature the task needs is exposed at the pin (else file a gap and stop that piece); if another game would need what you write, or you are unsure, ask the repo owner.
9. **Plan row:** starting a layer, set its row to In progress (<branch>) on its own small `docs/` branch, landed the same day so `dev` shows it (`Internal/Plan.md`).
10. **Done:** you know the check that proves the task.

### Before merging into `dev`

1. **Commit** by name, and shelve or discard the rest after asking (4.2): `dv status` shows no changes.
2. **`scripts\check-branch.ps1` passes.** It merges `dev` in first and builds what the merged tree needs; fix any conflict or issue on the branch and re-run it. Note the `dev` commit it merged.
3. **Your change list** is `dv diff --base dev --compare <branch> --name-status`.
4. **Builds run in PowerShell, not Git Bash,** and count only with `Compile` and `Link` lines for the changed module or an up-to-date report. By hand: `& "<engine>\Engine\Build\BatchFiles\Build.bat" TheConstructEditor Win64 Development -Project="<repo>\TheConstruct.uproject" -WaitMutex`, and `TheConstruct` for the game target.
5. **The feature's own check passes.**
6. **The change list is clean:** no section 1 marker, no build input naming the SDK's path, `Plugins/Crowdy` only on a `repin/` or `promote/` branch (else ask the repo owner); `scripts\check-sdk-pin.ps1` passes.
7. **The AI review** (`Internal/Harness/Review.md`) ran, and its findings are folded in.
8. **A `Changes/` fragment** (4.7) if the change is user-visible or the pin moved.
9. **The as-built note** records the verification and any deploy (section 9); a `docs/` branch of `Internal/` text has none and reports to its developer.
10. **The plan's status row** is updated if the change starts, finishes or defers a layer, or records a check result.
11. **Docs match the code** (section 10).
12. **Just before `dv merge <branch> --into dev`,** the first line of `dv show dev` still names the commit from item 2; if not, go back to item 2.
13. **After landing,** `dv checkout dev` (4.3).

### End of a session

Run `dv status` and ask about every pending file (4.2). Name each request that has not landed.

## 9. As-built notes

Record what was actually built, so the other developer and their agent can follow it without reading the whole change.

1. **Where:** `Internal/As-Built/<Category>/<Feature-Name>.md` (see `Internal/As-Built/README.md`): one note per feature, updated in place, in the same change as the code.
2. **What:** what it is, where it lives, how it works, its gate and version, the SDK surface it uses, gaps filed, gotchas.
3. **Verification:** the `scripts\check-branch.ps1` result, the build, the feature's check with its last result, the AI review and what became of its findings, whether the pin moved, and "SDK surface: only exposed symbols used; gaps filed: <ids or none>". Also "Deploys" (4.11) and, for a harness change, the dates of both yeses. Record only what has happened: a step not done yet reads "pending".
4. **Done:** the author verifies their own work and marks the note Done with the date; nobody signs off another's work. A layer that fails or overruns is Deferred by its author, with the date and a one-line reason in the plan's status table.
5. **Before working in a category,** read its notes; where a note and the code disagree, fix the note in your change.
6. **Internal:** notes may name register ids and people, never secrets.

## 10. Docs (the web Construct's procedure)

1. **`README.md` is the front door:** what the repo is, how to get set up, where everything else lives.
2. **`docs/` holds the user-facing pages,** added as features land (the expected list is in `Internal/Plan.md`). Never create an empty page.
3. **When code and a doc disagree, fix the doc in the same change.** A merge that leaves a doc wrong is not done.
4. **`README.md`, `docs/`, `Changes/` and `CHANGELOG.md` are public:** no register, plan or phase ids, nothing private.

## 11. Diversion basics

- **Run `dv` from the repo root, in PowerShell;** run `dv help <command>` before one you have not used. `dv` prints errors on stdout, so judge a call by its exit code and output.
- **`dv checkout` with pending changes** needs `--take-changes`, `--shelve-changes` or `--discard-changes` (never on work you did not make), or it asks a question a non-interactive shell cannot answer. Add `--ignore-shelf` when the target may hold a shelf.
- **No push:** Diversion syncs files and commits by itself; others' commits arrive by auto-update or `dv update`. A file sync is not a commit.
- **`dv diff --base A --compare B` compares two branch tips,** so A's newer work shows as B's deletions.
- **`dv log --oneline`** shows first-parent history only, 20 commits unless `-n` says more.
- **Diversion's documentation index:** https://docs.diversion.dev/llms.txt