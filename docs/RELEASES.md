# Releases

## Overview

VOXOV releases are automated with GitHub Actions when pushing a semantic version tag.

Supported tags:

- `0.0.1`
- `v0.0.1`

Workflow file:

- `.github/workflows/release.yml`

## Published artifacts

- `VOXOV-<tag>-linux-x86_64.tar.gz`
- `VOXOV-<tag>-windows-x86_64.zip`
- `VOXOV-<tag>-android-arm64-v8a.apk`

The Linux and Windows assets are runnable bundles (not bare binaries) and include runtime files needed at startup.

## Runtime validation policy

Release workflow validates startup from packaged artifact form before publishing.

Linux bundle checks include:

- executable + launcher script present
- required runtime files present
- unresolved `ldd` dependencies rejected
- startup smoke test (`xvfb-run ...`) must not hard-fail

Windows bundle checks include:

- `voxov.exe` present
- required runtime DLLs present
- process startup smoke test from extracted bundle

Android release checks include:

- release APK created
- APK contains `lib/arm64-v8a/libvoxov.so`

## Release title

Published title format:

- `VOXOV <tag> (Linux/Windows/Android)`

GitHub still auto-adds source archives (`Source code (zip/tar.gz)`) separately.

## Create and push a release tag

```bash
git tag 0.0.1
git push origin 0.0.1
```

Or:

```bash
git tag v0.0.1
git push origin v0.0.1
```

## Notes

- Release builds disable aggressive Jolt SIMD flags for broader CPU compatibility:
  - `USE_AVX2=OFF`
  - `USE_F16C=OFF`
  - `USE_FMADD=OFF`
  - `USE_LZCNT=OFF`
- Linux and Windows release bundles now use the same OpenGL-first desktop runtime.
