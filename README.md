## Install (Start Here)

### Windows
## 👉 Download: https://github.com/kyambuthia/voxov/releases/tag/0.0.26
- From release assets, download: `VOXOV-0.0.26-windows-x86_64.zip`
- Extract the archive.
- Launch `voxov.exe`.

### Android
## 👉 Download: https://github.com/kyambuthia/voxov/releases/tag/0.0.26
- From release assets, download: `VOXOV-0.0.26-android-arm64-v8a.apk`
- Open the APK on your device and allow installation from unknown sources if prompted.
- Launch `VOXOV`.

### Linux
## 👉 Download: https://github.com/kyambuthia/voxov/releases/tag/0.0.26
- From release assets, download: `VOXOV-0.0.26-linux-x86_64.tar.gz`
- Run:

```bash
tar -xzf VOXOV-0.0.26-linux-x86_64.tar.gz
cd VOXOV-0.0.26-linux-x86_64
./run.sh
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

## UI Audio Assets
- Tiny CC0 UI sounds are in `assets/audio/ui/`.
- Provenance, licensing, and hashes are documented in `assets/audio/ui/README.md`.

## Showcase
### Current Android Gameplay
Click the preview to open the small MP4 recording.

[![VOXOV Android Gameplay Video Preview](docs/media/voxov_android_gameplay_01.jpg)](docs/media/voxov_android_gameplay.mp4)

| Android Screenshot 1 | Android Screenshot 2 |
| --- | --- |
| ![VOXOV Android Screenshot 1](docs/media/voxov_android_gameplay_01.jpg) | ![VOXOV Android Screenshot 2](docs/media/voxov_android_gameplay_02.jpg) |

### Previous Showcase
![VOXOV Android Gameplay GIF](docs/media/voxov_android_gameplay.gif)

![VOXOV Gameplay GIF](docs/media/voxov_state.gif)

| Screenshot 1 | Screenshot 2 |
| --- | --- |
| ![VOXOV Screenshot 1](docs/media/voxov_state_01.png) | ![VOXOV Screenshot 2](docs/media/voxov_state_02.png) |
