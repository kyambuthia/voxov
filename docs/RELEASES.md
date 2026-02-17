# Releases

## Overview

VOXOV releases are automated with GitHub Actions when you push a semantic version tag.

Supported tag formats:

- `0.0.1`
- `v0.0.1`

The release workflow builds Linux artifacts and publishes them to GitHub Releases.

## What gets published

- `voxov-linux-x86_64-<tag>.tar.gz`
- `voxov-linux-x86_64-<tag>.tar.gz.sha256`

Archive contents:

- `voxov`
- `voxov_asset_cooker`
- `README.md`
- `LICENSE` (if present)

## Create a release tag

From repository root:

```bash
git tag 0.0.1
git push origin 0.0.1
```

or with `v` prefix:

```bash
git tag v0.0.1
git push origin v0.0.1
```

## Workflow file

- `.github/workflows/release.yml`

## Notes

- The release build disables aggressive Jolt SIMD flags for broader CPU compatibility:
  - `USE_AVX2=OFF`
  - `USE_F16C=OFF`
  - `USE_FMADD=OFF`
  - `USE_LZCNT=OFF`
