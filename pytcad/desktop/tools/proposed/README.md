# Proposed, NOT applied (NATIVE-DESKTOP-PLAN.md 26.9.3-26.9.4)

`ucrt-exclusion.patch` -- `stage.ps1 -ExcludeUcrt` (off by default) and `gui/tests/test_desktop_ucrt_exclusion.py`.
Nothing in `desktop/tools/` references it. It applies cleanly (`git apply --check -p1 <patch>` from the repo root)
and its 14 tests pass on a scratch tree with it overlaid. Apply ONLY after approval; then delete this folder.

`ucrt-92-files.txt` -- the exact files it would remove from `runtime\`.

Removing app-local UCRT files is a runtime change: after applying, the full S2 gate (`stage.ps1 -Reference ...
-ExcludeUcrt`) and the S3 Sandbox gate must be re-run.
