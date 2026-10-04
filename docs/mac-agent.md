# Instructions for Claude Code on the Mac build machine

You are running on a Mac that belongs to someone who is **not technical**. The project owner
controls this session remotely. Your job: keep a working macOS build of Transcript Studio,
rebuilt automatically whenever the owner pushes new commits from Windows.

Re-read this file after every pull — it may have been updated.

## Talking to people
- The Mac's owner may be sitting at the Mac. When they must do something (click a button, type
  their Mac password), say exactly what they will see and what to click, in one or two short,
  friendly sentences. Never assume they know what Terminal, Git or Homebrew are.
- The project owner reads your messages remotely: give them short, factual status updates.

## One-time setup (skip any step that is already done)
1. Check the Mac: `uname -m` (arm64 = Apple Silicon, uses the Metal GPU; x86_64 = Intel, CPU only),
   `sw_vers`, free disk space (`df -h ~`; need about 10 GB).
2. Xcode Command Line Tools: `xcode-select -p`. If missing, run `xcode-select --install`; a window
   appears on the Mac — ask the owner to click **Install** and **Agree**, then wait until
   `xcode-select -p` succeeds (poll every 30 s; it can take 10–20 minutes).
3. Homebrew: `command -v brew` (also check `/opt/homebrew/bin/brew` and `/usr/local/bin/brew`).
   If missing, the installer needs the Mac password, which you cannot type. Open it in a Terminal
   window for the owner:
   `osascript -e 'tell application "Terminal" to activate' -e 'tell application "Terminal" to do script "/bin/bash -c \"$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)\""'`
   and tell them: "A Terminal window opened. When it asks for your password, type your Mac
   login password (nothing appears while you type — that's normal) and press Return. If it asks
   to press Return to continue, press Return." Wait until brew exists, then add it to the PATH
   of your shell commands (`eval "$(/opt/homebrew/bin/brew shellenv)"` on Apple Silicon).
4. `brew install git ffmpeg` (ffmpeg is only used to make the self-test recording).
5. Get the code: `git clone --recurse-submodules https://github.com/utkarshyadav009/TranscriptStudio.git ~/TranscriptStudio`
   (if it already exists: `cd ~/TranscriptStudio && git pull --recurse-submodules`).

## Build
`cd ~/TranscriptStudio && scripts/build-mac.sh`
- First build takes 20–40 minutes (engine). Later builds: `scripts/build-mac.sh --skip-engine`
  unless the commit changed `third_party/NeMo-Speech.cpp` (then run the full script).
- The script ends with a self-test that transcribes a generated two-voice test file. Report the
  `done:` line it prints (words, speakers, speed) to the project owner.
- Success = "Built: …/dist/Transcript Studio.app". Then open it once (`open "dist/Transcript Studio.app"`)
  to confirm the window appears, and tell the owner where the app is.

## When the build fails
- The macOS code (`src/platform_mac.mm`, `src/audio_decode_mac.cpp`, `scripts/build-mac.sh`,
  CMake Apple parts) was written on Windows and never compiled on a Mac before. Fixing compile or
  link errors in those files is expected and allowed. Keep fixes small and Mac-only; do not change
  Windows code, the interface (`ui/`) or the engine submodule.
- Keep your fixes on a local branch `mac-fixes` on top of `origin/main` and commit them there
  (end each commit message with `Co-Authored-By: Claude <noreply@anthropic.com>`).
  Also write them to `~/TranscriptStudio-mac-fixes.patch` (`git format-patch origin/main --stdout`)
  and tell the project owner, so they can bring the fixes into the main repository.
- You have no permission to push to GitHub. Never push.
- If a problem needs a decision (not just a compile fix), stop and ask the project owner.

## Waiting for new commits (after the first successful build)
Run a background watcher that exits when the remote `main` changes, so you are woken up:
```bash
cd ~/TranscriptStudio
base=$(git ls-remote origin refs/heads/main | cut -f1)
while [ "$(git ls-remote origin refs/heads/main | cut -f1)" = "$base" ]; do sleep 120; done
echo "new commit on main"
```
When it fires: wait 3 minutes and check again (the owner often pushes several commits in a row;
wait until `main` stops changing), then:
1. `git checkout mac-fixes 2>/dev/null || git checkout -b mac-fixes origin/main`
2. `git fetch origin && git rebase origin/main` — if a conflict appears, the owner probably merged
   your fix upstream: keep the upstream version (`git rebase --skip` for that commit).
3. `git submodule update --init --recursive third_party/NeMo-Speech.cpp`
4. Build (see above), report the result, then start the watcher again.

## Rules
- Never upload, copy or commit recordings, transcripts (`.tsproj`), Word exports or anything in
  `~/Documents/Transcript Studio` — that is private research data, often children's voices.
- Do not install anything beyond: Xcode Command Line Tools, Homebrew, and the Homebrew packages named
  in this file and in `scripts/build-mac.sh`.
- Do not change system settings, and do not delete anything outside `~/TranscriptStudio`.
