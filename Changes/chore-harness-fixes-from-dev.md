SDK: crowdy-sdk dv.commit.545, dev after 2.21.0.

- Every fix, an urgent one included, is cut from `dev` and promoted like any other change; there are no hotfix
  branches. `scripts/new-branch.ps1` and `scripts/check-branch.ps1` no longer treat `fix/hotfix-` names specially.
