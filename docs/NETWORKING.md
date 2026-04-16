# Networking Guide

## Overview

VOXOV currently uses an authoritative server model over ENet, with the networking code split into distinct layers:

- `engine_net_proto`: packet headers, protocol versioning, feature/session flags, and POD wire structs
- `engine_net_transport`: ENet client/server transport (`NetClient`, `NetServer`)
- `engine_net_discovery`: LAN discovery
- `engine_server`: authoritative server session/state
- `engine_net`: umbrella target that links discovery + transport for callers

At runtime:

- Server owns world/network truth.
- Clients send input ticks.
- Server sends player snapshots/states and chunk interest responses.
- Transport uses reliable and unreliable channels.

## Runtime Modes

- Standalone dedicated server:
  - `./build/desktop/main/bin/voxov_server --port 7777`
- Combined client + server in one process:
  - `./build/desktop/main/bin/voxov --server`
- Compatibility headless server mode inside the desktop client:
  - `./build/desktop/main/bin/voxov --headless-server --port 7777`
- Client connect:
  - `./build/desktop/main/bin/voxov --connect <SERVER_IP> --port 7777`

## LAN Bring-up (2-3 machines)

Server machine:

```bash
./build/desktop/main/bin/voxov_server --port 7777
```

Client machine A:

```bash
./build/desktop/main/bin/voxov --connect <SERVER_LAN_IP> --port 7777 --devhud
```

Client machine B:

```bash
./build/desktop/main/bin/voxov --connect <SERVER_LAN_IP> --port 7777 --devhud
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
- `PlayerRemove`: player removal broadcast.
- `ChunkInterest`: client chunk-interest request.
- `ChunkState`: server chunk-state response.
- `ProtocolInfo`: protocol/version/feature handshake data.
- `SessionInfo`: server/session metadata.

See:

- `src/engine_net_proto/net_types.hpp`
- `src/engine_net_proto/net_protocol_helpers.hpp`
- `src/engine_net/net_client.cpp`
- `src/engine_net/net_server.cpp`
- `src/engine_net/lan_discovery.cpp`
- `src/engine_server/server_session.cpp`

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
  - keep `--devhud` enabled and inspect `dt`, `fixed_dt`, and remote count
  - compare frame time, render time, and fixed-step spikes across machines

## Notes

- Current networking is still foundation-level and still WIP.
- Protocol framing, versioning, packet headers, and feature/session metadata are now explicit in `engine_net_proto`.
- The recent refactor direction is to keep protocol, transport, discovery, and server-session responsibilities separated while Android/Web move toward the same runtime model.
