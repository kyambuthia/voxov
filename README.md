## VOXOV
Download builds: https://github.com/kyambuthia/voxov/releases

## Latest Release (0.0.20)
- Release page: https://github.com/kyambuthia/voxov/releases/tag/0.0.20
- Android APK: https://github.com/kyambuthia/voxov/releases/download/0.0.20/VOXOV-0.0.20-android-arm64-v8a.apk
- Windows ZIP: https://github.com/kyambuthia/voxov/releases/download/0.0.20/VOXOV-0.0.20-windows-x86_64.zip
- Linux binary: https://github.com/kyambuthia/voxov/releases/download/0.0.20/VOXOV-0.0.20-linux-x86_64

## Install
- Android (arm64): download APK, allow install from unknown sources, install, launch `VOXOV`.
- Windows (x86_64): download ZIP, extract, run `voxov.exe`.
- Linux (x86_64): download binary, then run:

```bash
chmod +x VOXOV-0.0.20-linux-x86_64
./VOXOV-0.0.20-linux-x86_64
```

## Setup From Source
- Clone with submodules:

```bash
git clone --recurse-submodules github.com/kyambuthia/voxov.git
cd voxov
```

- Build (desktop):

```bash
mkdir build && cd build
cmake .. && cmake --build .
```

- Android setup/build details: `docs/ANDROID.md`
- Full setup guide: `docs/SETUP.md`

## Docs
- Docs overview and setup: `docs/SETUP.md`
- Run and test: `docs/RUNNING.md`
- Android: `docs/ANDROID.md`
- Build/release details: `docs/RELEASES.md`
- Full docs folder: `docs/`

## Showcase
![VOXOV Android Gameplay GIF](docs/media/voxov_android_gameplay.gif)

![VOXOV Gameplay GIF](docs/media/voxov_state.gif)

| Screenshot 1 | Screenshot 2 |
| --- | --- |
| ![VOXOV Screenshot 1](docs/media/voxov_state_01.png) | ![VOXOV Screenshot 2](docs/media/voxov_state_02.png) |
