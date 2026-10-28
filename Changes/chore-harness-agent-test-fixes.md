SDK: crowdy-sdk dv.commit.545, dev after 2.21.0.

- `scripts/check-branch.ps1` builds the editor and game targets by itself when the change needs it: anything under
  `Source/` or `Config/`, a `.uproject` or `.uplugin`, or the SDK pin. `-Build` and `-Game` still force a build.
- `scripts/check-branch.ps1` prints the exact commands to land a branch when an agent's merge is blocked, and the pin
  and SDK-surface lines for the record.
- `scripts/new-branch.ps1` reminds the agent to flag before its first edit in the other developer's area.
