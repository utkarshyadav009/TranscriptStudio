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

## When the build fails: fix it yourself
The macOS code (`src/platform_mac.mm`, `src/audio_decode_mac.cpp`, `scripts/build-mac.sh`, the
`APPLE` parts of `CMakeLists.txt`) was written on Windows. Fixing compile, link, bundling or
self-test errors is your job — keep going until the build and the self-test pass.
- **Mac-only files**: change freely (`platform_mac.mm`, `audio_decode_mac.cpp`, `build-mac.sh`,
  `get-models.sh`, `if(APPLE)` blocks in `CMakeLists.txt`).
- **Shared files** (`src/*.cpp`/`*.h`, `CMakeLists.txt` outside `if(APPLE)`): only small,
  portable fixes that are clearly correct on Windows too (a missing `#include`, a standard-library
  call that only MSVC accepts). Guard anything Apple-specific with `#if defined(__APPLE__)`.
  Never change behaviour, the interface (`ui/`) or the engine submodule.
- If a fix would need a design decision, stop and ask the project owner instead.

## Committing and pushing (only once a deploy key is set up — see the owner's prompt)
- Work on `main`. Before every push: `git fetch origin && git rebase origin/main`, rebuild if
  the rebase brought in new commits, then push. Never force-push. Never rewrite pushed history.
- Push only when `scripts/build-mac.sh` (and its self-test) passes with your change.
- Commit message: what was broken on macOS and how it is fixed, ending with
  `Co-Authored-By: Claude <noreply@anthropic.com>`. Commit as `Mac build agent <mac-build@transcriptstudio.local>`
  (repository-local `git config user.name/user.email`).
- Never commit build output, models, recordings, transcripts or anything personal.
- After pushing, tell the project owner the commit id and a one-line summary.
- Without a working deploy key: keep fixes on a local branch `mac-fixes` and write
  `~/TranscriptStudio-mac-fixes.patch` (`git format-patch origin/main --stdout`) instead.

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
1. `git checkout main && git pull --rebase origin main` (ignore your own pushes: if the only new
   commits are yours, there is nothing to rebuild).
2. `git submodule update --init --recursive third_party/NeMo-Speech.cpp`
3. If the new commits only touch `docs/` or `*.md`, re-read this file and go back to watching.
4. Otherwise build (see above). If it fails, fix it as described above. Report the result, then
   start the watcher again.

## Rules
- Never upload, copy or commit recordings, transcripts (`.tsproj`), Word exports or anything in
  `~/Documents/Transcript Studio` — that is private research data, often children's voices.
- Do not install anything beyond: Xcode Command Line Tools, Homebrew, and the Homebrew packages named
  in this file and in `scripts/build-mac.sh`.
- The only credential you use is this repository's deploy key (`~/.ssh/transcriptstudio_deploy`).
  Never print or share its private part.
- Do not change system settings, and do not delete anything outside `~/TranscriptStudio`.
