# Repository Guidelines

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

Before committing to `trunk`, run the narrowest useful verification for the change. For normal desktop engine work, the default is:

```bash
cmake --build build/desktop/main --parallel
ctest --test-dir build/desktop/main --output-on-failure
```

If the relevant build directory does not exist, configure it first:

```bash
cmake -S . -B build/desktop/main -DVOXOV_BUILD_TESTS=ON
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
