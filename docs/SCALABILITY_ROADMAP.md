# Planet-to-galaxy scalability roadmap

VOXOV should scale by keeping a small active simulation bubble, not by loading
or expressing a galaxy as one giant world. Procedural content outside that
bubble should be represented by stable identifiers, seeds, and compact metadata.

## Non-negotiable architecture

- Address space: `GalaxyId -> SystemId -> BodyId -> RegionId -> ChunkAddress`.
  Gameplay and networking must carry this identity separately from local
  coordinates.
- Position: `(frame/address, local dvec3)`. Never store a meter-scale position
  relative to a galaxy origin. GPU positions remain camera-relative `float`.
- Streaming: only the active body keeps voxel chunks and collision resident.
  Nearby bodies use bounded macro meshes; inactive systems are catalog records.
- Authority: the server owns simulation and persistent mutations for active
  regions. Clients predict inputs and interpolate replicated state.
- Generation: all immutable terrain derives from versioned generator inputs.
  Persistence stores edits and entity state, not regenerated base voxels.
- Sharding: a star system is the first server simulation boundary. Galaxy
  services route players and durable metadata; they do not tick every system.

## Findings from the current source

### Completed foundation: protocol v7 identity and encoding

Protocol v7 now uses an explicit fixed-width little-endian codec rather than
raw C++ object layouts. Player states and snapshots carry a system/body/frame
address with body-local `double` positions, while chunk identity includes body,
cube face, shell, and three local axes. Golden-byte and truncation tests cover
the wire contract. Generator/build compatibility negotiation and fuzzing remain
before mixed-version public servers are safe.

### P0: finish unifying server and client world ownership

`ServerSession` now owns Voxov and Aster `BlockWorld` instances, derives
body-scale spherical collision and spawn points from them, rejects
client-authored transforms, and owns replicated player state from validated,
sequenced inputs. However, detailed procedural terrain collision and world
setup are still client-side or duplicated between server and graphical runtime,
`RuntimeWorldState` retains a legacy 3x3 flat stream, clients do not reconcile
prediction, and voxel edits remain client-local. Move configuration, fixed-step
movement, edits, and region interest behind one shared headless simulation
interface.

### P0: persistence needs region-scale journaling

The active game now stores a versioned, checksummed snapshot with atomic
replacement for expedition progress, player body-local position, character,
and sparse edits on Voxov and Aster. The next persistence boundary is a
server-owned region journal with compaction and per-region recovery; the current
single-player snapshot should not become the multi-instance storage format.

### P1: frame hierarchy is hard-coded to one system

`CoordinateFrameManager` now preserves body-specific origins during transforms,
and the compact flight path can swap between the Voxov and Aster terrain
runtimes. The remaining identity is still based on numeric body indices, while
`SolarSystem` stores absolute heliocentric `dvec3` positions and linearly scans
bodies for sphere-of-influence checks. Replace numeric vector indices with
stable body IDs and a general parent-frame graph; keep coordinates parent-local.

### P1: replication is quadratic

`NetServer::broadcast_player_states` loops over every subject and observer.
This is acceptable at the current 32-player cap, not for crowded regions.
Maintain a spatial interest index per active region and generate observer sets
once per replication tick. Replicate frame changes reliably and local motion
unreliably.

### P1: terrain work still lives in the render frame

`Engine::tick` creates and sorts several desired/resident/mesh-work containers,
generates chunks, and builds meshes synchronously. It also owns simulation,
networking, presentation, and streaming. Cache the stream plan until its current
or predicted chunk changes, then move generation and meshing into bounded job
queues with generation-version cancellation. The renderer should consume
immutable upload batches.

`BlockWorld::collect_stream_chunks` previously used a linear duplicate scan for
every candidate; it now uses a hash set, changing that part from quadratic to
expected linear time.

### P2: scene ownership causes repeated scans and copies

Opaque meshes are stored in one vector and repeatedly erased/searched by mesh
ID. Split stable global meshes, streamed chunk instances, dynamic entities, and
transient debug geometry. A chunk render registry keyed by `ChunkAddress` should
update only changed handles.

### P2: scale budgets need to be explicit

Add telemetry and regression budgets for resident voxel bytes, generated and
meshed chunks per second, upload bytes, draw calls, server tick time, replication
bytes by message type, interest-set size, persistence latency, and job backlog.
Performance work without these budgets will move bottlenecks rather than bound
them.

## Delivery sequence

1. Multiplayer test releases: ship matching desktop client/server binaries,
   end-to-end smoke them, and publish checksums.
2. Protocol v7: explicit serialization, world/body/frame identity, and golden
   wire tests are complete; add build/generator compatibility and fuzz tests.
3. Shared headless planet simulation: move `BlockWorld`, collision, fixed-step
   movement, edits, and region interest behind a runtime interface used by both
   client and dedicated server.
4. Region persistence and jobs: journal edits, snapshot atomically, and add
   cancellable generation/meshing queues with memory budgets.
5. Multi-body systems: stable body graph, parent-local coordinates, per-body
   stream state, and transition replication.
6. Galaxy routing: deterministic system catalogs, system-server leases,
   handoff tokens, and dormant-system metadata. Do not simulate inactive
   systems frame by frame.

The immediate rule is simple: every new gameplay feature must name its world
frame and authority owner, and must have a bounded resident-memory and per-tick
work budget.
