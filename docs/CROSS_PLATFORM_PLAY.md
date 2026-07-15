# Cross-platform builds and play

VOXOV builds the same C++ game runtime and protocol for Linux, Windows,
Android, and WebAssembly. Protocol version 6 relays each client's spherical
planet transform through an ENet server, so clients on every target share
player presence while terrain remains deterministic from the session seed.

## Build targets

| Target | Build command | Output |
|---|---|---|
| Linux | `cmake -S . -B build/linux -G Ninja && cmake --build build/linux --parallel 2` | `build/linux/bin/voxov` |
| Windows | `cmake -S . -B build/windows -G Ninja && cmake --build build/windows --parallel 2` | `build/windows/bin/voxov.exe` |
| Android | `./android/gradlew -p android :app:assembleDebug --no-daemon` | `android/app/build/outputs/apk/debug/app-debug.apk` |
| Web | `emcmake cmake -S . -B build/web/main -G Ninja && cmake --build build/web/main --parallel 2` | `build/web/main/bin/voxov_web.html` |

CI compiles and tests Linux and Windows, builds the WebAssembly target, and
boots the APK in an Android emulator. Tagged releases package all four targets;
the Linux and Windows archives contain both the client and `voxov_server`.

## Test a downloaded desktop release

Extract the same tagged release on every desktop. On the host:

```bash
# Linux convenience script (equivalent to ./voxov_server --lan)
./host.sh --port 7777
```

```powershell
# Windows
.\voxov_server.exe --lan --port 7777
```

On each Linux client:

```bash
./run.sh --connect HOST_IP --port 7777
```

On each Windows client:

```powershell
.\voxov.exe --connect HOST_IP --port 7777
```

Allow inbound UDP port 7777 on the host firewall. Everyone must use artifacts
from the same tag because protocol compatibility is exact, not negotiated.

The current multiplayer slice is intended for connection and shared-player
presence testing. Compact-planet terrain and movement are still simulated by
each client, and the server validates bounds rather than authoritatively
simulating the full spherical `BlockWorld`. Do not treat this build as
cheat-resistant or persistent-world multiplayer yet.

## Native cross-play

Start a dedicated server, or choose **Host LAN** in a native client:

```bash
./build/linux/bin/voxov_server --port 7777 --lan
./build/linux/bin/voxov --connect 127.0.0.1 --port 7777
```

Windows, Linux, and Android clients use ENet UDP directly. Open UDP port 7777
on the server host when clients are not on the same machine.

## Browser cross-play gateway

Browsers cannot open UDP sockets. The web build uses Emscripten's full POSIX
socket proxy to carry ENet traffic over a WebSocket to a gateway, which then
forwards UDP to the same VOXOV server.

Start the game server, gateway, and static web server:

```bash
./build/linux/bin/voxov_server --port 7777 --lan
./scripts/run_web_gateway.sh 8080
go run ./scripts/serve.go
```

Then open:

```text
http://127.0.0.1:8088/voxov_web.html?server=127.0.0.1&port=7777&proxy=ws://127.0.0.1:8080
```

For an HTTPS deployment, terminate TLS in front of the gateway and pass a
`wss://` proxy URL. Do not expose an unencrypted `ws://` gateway from an HTTPS
page; browsers block mixed active content. Browser clients join a native or
dedicated server and cannot host an ENet session themselves.

The included web server sends the cross-origin isolation headers required by
the WebAssembly pthread used for proxied POSIX sockets.
Set `VOXOV_WEB_ROOT` or `VOXOV_WEB_ADDR` to serve a different build directory
or listen address.

All participants must run the same protocol version. The server rejects
malformed, non-finite, or extreme client transforms before replication.
