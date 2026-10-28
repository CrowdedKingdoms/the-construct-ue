Repin: crowdy-sdk dv.commit.571, 2.21.1 (on SDK `dev`, before its `prod` tag).

- Blueprint sign-in starts from the Game Mode: the SDK docs no longer sign in or bind events from a Game Instance's
  Event Init, which runs before the SDK's subsystems exist. Construct action: none required; the shell's binds already
  run after Init.
- `-crowdysession=<name>` gives a packaged or standalone client its own saved session, so two clients sharing a
  `Saved` folder can sign in as different accounts. Construct action: none required; a test setup may use it instead
  of a separate folder or `-userdir`.
- A deploy no longer restarts removed Server Object types first, because the platform stops them itself; the deploy
  result drops `stoppedRemoved` and `stopRemovedFailed`, and `-op=instances` shows `orphaned`. Construct action: none
  required; nothing here reads those fields.
- CrowdyCPP 0.74.0. Construct action: none required.
