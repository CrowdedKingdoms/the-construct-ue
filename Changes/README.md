# Changes

A change fragment is a short file that says what one branch changes for people who use the Construct. Every branch that
needs one adds its own, so two branches never edit the same file. The repo owner folds the fragments into `CHANGELOG.md` at release and
deletes them.

## When one is needed

Any user-visible change, and any move of the SDK pin. A branch whose change nobody using the Construct could notice needs
no fragment. A new or changed script under `scripts/` counts (it is mirrored); a change only under `Internal/` never does.

## File name

`Changes/<branch name with / replaced by ->.md`. The branch `feature/presence-pawn` gives
`Changes/feature-presence-pawn.md`. This README is the only file here that is not a fragment. Never edit `CHANGELOG.md`
directly on a work branch.

## Entry shape

Prose. It opens with the SDK pin, after the word `repin` when the branch moves it: crowdy-sdk `dv.commit.N` and the SDK
version. Write `2.21.0` for a release, or `dev after 2.21.0` for a `dev` pin, since the SDK's version changes only at a
release. A work branch sets no version, so a fragment carries none; the repo owner adds the version line when folding.

Then a short list of what changed. A repin lists what the SDK change brings, with an action for each item ("none
required" included). Both version lines are pre-1.0, so say so when a minor bump may break things. Write no tracker, task
or register ids, app ids, hostnames or emails: this file is public.

Example, `Changes/feature-presence-pawn.md`:

```markdown
SDK: crowdy-sdk dv.commit.545, dev after 2.21.0.

- Players now appear as simple pawns that other clients can see move.
- The pawn shows the player's name above it.
```

## Folding

At release the repo owner moves the fragments into `## Unreleased` in `CHANGELOG.md`, newest first, on a
`chore/release-*` or `repin/*` branch, and deletes them in the same change. A promotion adds the destination tier's pin
as a new line and never rewrites an existing line.
