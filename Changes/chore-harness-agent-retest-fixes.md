SDK: crowdy-sdk dv.commit.545, dev after 2.21.0.

- `scripts/check-branch.ps1` says an agent lands a branch only on a yes given after its report and the AI review, and
  treats a "permission not granted" reply like any other block.
- Claude Code's auto-memory is off for this repo, so `AGENTS.md` stays the only place rules live.
