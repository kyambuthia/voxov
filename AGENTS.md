# Repository Guidelines

## Non-Negotiable Commit Discipline

This repository values inspectable history over convenience. Do not create
large mixed commits. Do not hide uncertainty in a commit. Do not claim a change
is complete unless the verification proves the exact claim.

Before committing, the agent must:

- Run `git status --short` and inspect every changed and untracked path.
- Split unrelated work into separate commits, even if the user asks to "commit".
- Stage files intentionally with pathspecs or `git add -p`; never use `git add .`
  for non-trivial work.
- Review the staged diff with `git diff --cached --stat` and
  `git diff --cached` before committing.
- Tell the user the exact commit split when more than one logical change is
  present.
- Refuse to make a single commit when the diff mixes unrelated architecture,
  gameplay, rendering, tests, build files, docs, or generated assets.
- Preserve user edits. If a file has mixed user and agent edits, inspect it and
  stage only the intended hunks.

The following are not acceptable on `trunk`:

- "Everything I touched" commits.
- Rewrite commits that also include drive-by cleanup.
- Test commits bundled with engine behavior unless the tests are inseparable
  from the same small change.
- Formatting churn mixed with behavior changes.
- Submodule changes mixed with source changes.
- Debug prints, temporary instrumentation, or placeholder assets unless the
  commit subject says they are intentional development tooling.
- Commits that pass only because broken or flaky tests were ignored without
  saying so in the commit body.

If the working tree is already messy, the agent must first produce a commit
partition plan. The plan must list commit subjects and the files or hunks each
commit owns. Only then stage the first commit.

## Planet Rewrite Commit Rules

Planet work is especially high-risk and must be split aggressively. The
following areas require separate commits unless the diff is tiny and physically
inseparable:

- Coordinate math and data types.
- Terrain generation and voxel meshing.
- Planet streamer, quadtree, LOD selection, and residency.
- Renderer resource management, shaders, GPU upload, and draw behavior.
- Player movement, fly mode, camera alignment, and controls.
- Collision, raycast, and physics integration.
- Atmospheric transition state.
- Tests and documentation.

Do not commit fake planet behavior without naming it as scaffolding in the
subject or body. Do not describe a debug shell, impostor, flat patch, analytic
collider, or synchronous generator as a finished spherical voxel planet.

## Commit Strategy

This repository is a single-developer, trunk-based C++ game engine project. The goal is a clean linear history where `trunk` is always buildable and every commit is useful to inspect or bisect.

### Branching

- `trunk` is the main line. It must always compile, should be shippable, may be pushed directly, and must never be force-pushed.
- `wip/<name>` is for multi-session experiments or larger work that should not live on `trunk` yet.
- `fix/<bug>` is for non-trivial bug fixes that need isolation before landing.
- Keep history linear. Before landing `wip/*` or `fix/*`, rebase onto current `trunk`, run verification, then fast-forward merge into `trunk`.
- Scratch commits are allowed on `wip/*` and `fix/*` while working, but clean them up before merging. No WIP, checkpoint, or scratch commits on `trunk`.

### Commit Prefixes

Use a short prefix that identifies the primary area changed:

- `fix:` Bug fixes.
- `deps:` Submodule bumps and dependency additions, removals, or version changes.
- `build:` CMake, toolchains, compiler flags, build scripts, and packaging mechanics.
- `ci:` GitHub Actions and other automation that runs outside the local build.
- `render:` GPU pipeline, shaders, meshes, materials, frame setup, and draw calls.
- `engine:` Core subsystems such as physics, networking, world state, runtime, assets, audio, and server internals.
- `gameplay:` Player movement, game rules, input behavior, camera behavior, HUD, and player-facing UI.
- `platform:` Desktop, Android, Web, and OS integration code.
- `test:` Unit tests, integration tests, test fixtures, and test-only tools.
- `assets:` Game assets and asset metadata.
- `docs:` Documentation and developer notes.
- `refactor:` Restructuring without intended behavior changes.
- `chore:` Repository maintenance such as `.gitignore`, formatting-only changes, comment-only changes, and other housekeeping.

Prefer these prefixes over inventing new ones. If a change spans areas, pick the prefix for the reason the commit exists, not every file it touches.

### Commit Shape

- Make one logical change per commit.
- Use a single-sentence subject line after the prefix, written in the imperative mood when practical.
- Keep the subject concise and do not add a trailing period.
- Every commit on `trunk` must compile. Avoid broken intermediate commits because they make bisecting unreliable.
- Submodule pointer updates get their own `deps:` commits. If code changes are needed to adapt to a dependency bump, put those in a separate follow-up commit.
- Bug fixes should include the symptom and verification. Put this in the commit body when the subject cannot carry both clearly:

```text
fix: avoid null world access during shutdown

Symptom: Closing the client during world load could crash in shutdown.
Verification: Built desktop target and reproduced clean shutdown locally.
```

### Verification

Before committing to `trunk`, run the narrowest useful verification for the change. The canonical local build layout is:

```text
build/
  desktop/main/
  web/main/
  android/main/
```

Keep generated CMake state inside those target-specific directories. Do not configure directly into `build/`, `build/desktop/`, or a root-level `build-web/` directory.

For normal desktop engine work, the default is:

```bash
cmake -S . -B build/desktop/main -DVOXOV_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build/desktop/main --parallel
ctest --test-dir build/desktop/main --output-on-failure
```

For Web via Emscripten, use the emsdk environment and keep the Emscripten cache writable under the build tree:

```bash
source /usr/local/emsdk/emsdk_env.sh
export EM_CACHE="$PWD/build/web/main/cache"
emcmake cmake -S . -B build/web/main -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/web/main --parallel
test -f build/web/main/bin/voxov_web.html
```

For Android, prefer `ANDROID_HOME`, then `ANDROID_SDK_ROOT`, then `~/Android/Sdk`; use the NDK CMake toolchain and verify configure at minimum:

```bash
ANDROID_SDK="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-$HOME/Android/Sdk}}"
ANDROID_NDK="$ANDROID_SDK/ndk/26.1.10909125"
cmake -S . -B build/android/main -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-29 \
  -DCMAKE_BUILD_TYPE=Debug \
  -DVOXOV_ENABLE_VULKAN=OFF
cmake --build build/android/main --parallel
```

Use platform-specific verification when touching Android, Web, packaging, or CI behavior. If a commit cannot be fully verified locally, say exactly what was verified and what remains unverified in the commit body.

### Recommended Landing Flow

For a small direct-to-trunk change:

```bash
git switch trunk
git pull --ff-only
# edit, build, test
git commit
git push
```

For a branch:

```bash
git switch trunk
git pull --ff-only
git switch wip/<name>
git rebase trunk
# clean up commits, build, test
git switch trunk
git merge --ff-only wip/<name>
git push
```
