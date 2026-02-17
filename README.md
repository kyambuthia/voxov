## Install (Start Here)

### Windows
## 👉 Download: https://github.com/kyambuthia/voxov/releases/tag/0.0.20
- From release assets, download: `VOXOV-0.0.20-windows-x86_64.zip`
- Extract the archive.
- Launch `voxov.exe`.

### Android
## 👉 Download: https://github.com/kyambuthia/voxov/releases/tag/0.0.20
- From release assets, download: `VOXOV-0.0.20-android-arm64-v8a.apk`
- Open the APK on your device and allow installation from unknown sources if prompted.
- Launch `VOXOV`.

### Linux
## 👉 Download: https://github.com/kyambuthia/voxov/releases/tag/0.0.20
- From release assets, download: `VOXOV-0.0.20-linux-x86_64`
- Run:

```bash
chmod +x VOXOV-0.0.20-linux-x86_64
./VOXOV-0.0.20-linux-x86_64
```

## About VOXOV
VOXOV is a cross-platform multiplayer co-op voxel game currently in active development.
The project targets desktop, mobile, and console-class platforms from a shared codebase.

## Setup From Source
- Clone the repository with submodules:

```bash
git clone --recurse-submodules github.com/kyambuthia/voxov.git
cd voxov
```

- Build (desktop):

```bash
cmake -S . -B build/desktop/main
cmake --build build/desktop/main --parallel
```

- Android setup and build details: `docs/ANDROID.md`
- Web setup and build details: `docs/WEB.md`
- Full setup guide: `docs/SETUP.md`

## Docs
- Docs overview and setup: `docs/SETUP.md`
- Run and test: `docs/RUNNING.md`
- Android: `docs/ANDROID.md`
- Web: `docs/WEB.md`
- Build/release details: `docs/RELEASES.md`
- Full docs folder: `docs/`

## Showcase
![VOXOV Android Gameplay GIF](docs/media/voxov_android_gameplay.gif)

![VOXOV Gameplay GIF](docs/media/voxov_state.gif)

| Screenshot 1 | Screenshot 2 |
| --- | --- |
| ![VOXOV Screenshot 1](docs/media/voxov_state_01.png) | ![VOXOV Screenshot 2](docs/media/voxov_state_02.png) |
