# Networking Guide

## Overview

VOXOV currently uses an authoritative server model over ENet.

- Server owns world/network truth.
- Clients send input ticks.
- Server sends player snapshots/states and chunk interest responses.
- Transport uses reliable and unreliable channels.

## Runtime Modes

- Combined client + server in one process:
  - `./build/desktop/main/bin/voxov --server`
- Headless dedicated server:
  - `./build/desktop/main/bin/voxov --headless-server --port 7777`
- Client connect:
  - `./build/desktop/main/bin/voxov --connect <SERVER_IP> --port 7777 --renderer vulkan`

## LAN Bring-up (2-3 machines)

Server machine:

```bash
./build/desktop/main/bin/voxov --headless-server --port 7777
```

Client machine A:

```bash
./build/desktop/main/bin/voxov --renderer vulkan --connect <SERVER_LAN_IP> --port 7777 --devhud
```

Client machine B:

```bash
./build/desktop/main/bin/voxov --renderer gl --connect <SERVER_LAN_IP> --port 7777 --devhud
```

## Validation Checklist

- Client logs contain `Assigned network player id=...`.
- `--devhud` `REM` count is `>= 1` when peers are connected.
- Movement from one client updates remote capsules on other clients.
- Server process remains running and does not report bind/init failures.

## Packet Types (Current)

- `Input`: client input tick to server.
- `Snapshot`: server authoritative local-player snapshot to client.
- `AssignPlayer`: server-assigned network id.
- `PlayerState`: replicated player state broadcast.
- `ChunkInterest`: client chunk-interest request.
- `ChunkState`: server chunk-state response.

See:

- `src/engine_net/net_common.hpp`
- `src/engine_net/net_client.cpp`
- `src/engine_net/net_server.cpp`

## Troubleshooting

- Cannot connect:
  - verify server IP and port
  - verify firewall allows UDP traffic on server port
  - confirm server started without ENet init/listen errors
- Connects but no remote movement:
  - run both clients with `--devhud`
  - check assignment logs and `REM` value
  - ensure both clients are connected to same server endpoint
- High jitter:
  - compare Vulkan vs OpenGL client paths
  - keep `--devhud` enabled and inspect `dt`, `fixed_dt`, and remote count

## Notes

- Current networking is foundation-level and still WIP.
- Protocol versioning and stronger serialization validation are planned improvements.
