---
name: github-push
description: >-
  Safely verify, stage, commit, and push changes to GitHub (origin) for
  uni-usb-dongle. Handles Windows git path fallback, diff/secret hygiene, test
  verification, non-destructive git practices, and clear post-push reporting.
---

# Push Code to GitHub

Use this skill when committing and pushing verified changes in `uni-usb-dongle` to the GitHub remote (`origin`: `https://github.com/ting1996/uni-usb-dongle.git`).

## Step 1: Resolve `git` Executable

On Windows, `git` may not be in the restricted shell `PATH`. Resolve it first without changing repo state:
- Linux / standard `PATH`: `git`
- Windows fallback: `C:\Program Files\Git\cmd\git.exe`

```powershell
$GitExe = if (Get-Command git -ErrorAction SilentlyContinue) { "git" } else { "C:\Program Files\Git\cmd\git.exe" }
```

Keep local read-only and commit commands inside the sandbox; run only network commands (`git fetch`, `git push`) outside the sandbox when required.

## Step 2: Pre-Push Hygiene & Verification

1. Inspect repository state:
   ```powershell
   & $GitExe status -uall
   & $GitExe branch --show-current
   & $GitExe remote -v
   ```
2. Review every uncommitted change (`git diff` and `git diff --cached`):
   - Confirm no build outputs (`/build/`, `/firmware/build/`), Python caches (`__pycache__/`, `*.pyc`), temporary logs, or binary dumps are queued to commit.
   - Confirm no secrets, credentials, tokens, `.env` files, or `.aws/` files are present or staged.
   - If firmware files (`include/dongle/`, `src/core/`, `firmware/`) changed, verify `web/`, `tools/`, and `tests/` are synchronized as required.
3. Confirm automated verification passed after the latest edits (e.g., `.\tools\windows.ps1 test` and/or `.\tools\windows.ps1 build -Target All -Config Release`), or explicitly inform the user if checks were skipped.

## Step 3: Stage & Commit

1. Stage intended files explicitly (`git add <paths>`); avoid blindly staging untracked artifacts with `git add -A` unless you verified `git status -uall` shows only intended files.
2. Check staged changes before committing:
   ```powershell
   & $GitExe diff --cached --stat
   ```
3. Commit with a clear, concise commit message describing what changed and why:
   ```powershell
   & $GitExe commit -m "<clear summary>"
   ```
4. **Safety rules**:
   - Never rewrite or amend already-published commits unless the user explicitly asks.
   - Never force-push (`--force` or `--force-with-lease`) to `main` or shared branches unless explicitly requested.

## Step 4: Sync Check & Push

1. Check remote status if needed (`& $GitExe fetch origin`).
   - If the branch has diverged or has merge/rebase conflicts, **stop and ask the user** how to proceed rather than force-pushing.
2. Push the current branch:
   ```powershell
   & $GitExe push origin <branch>
   ```
   *(For a brand-new branch with no upstream tracking yet, use `git push -u origin <branch>`.)*
3. If authentication or network access fails, **stop and report the exact error** to the user; never bypass credential or access controls.

## Step 5: Post-Push Verification & Report

Run:
```powershell
& $GitExe status -sb
& $GitExe log -1 --oneline
```
Report to the user:
- Pushed branch and remote (`origin/<branch>`)
- Commit hash and commit message
- Summary of pushed files and verification status
