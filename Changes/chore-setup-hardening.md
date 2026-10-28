SDK: crowdy-sdk dv.commit.545, dev after 2.21.0.

- `AGENTS.md` works for any coding agent: Claude Code, Gemini CLI, Codex, Grok Build and others load the same rules.
- A pre-commit hook refuses commits on a tier, AI attribution, em-dashes, private keys and console-managed network keys.
- `scripts/new-branch.ps1` cuts a work branch; `scripts/check-branch.ps1` merges `dev` in and checks a branch before it
  lands.
- The project enables the Diversion editor plugin as optional.
