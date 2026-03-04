# VOXOV Engine Vision

VOXOV is evolving from a learning project into a production-oriented rendering/game engine.
The short-term target is a tiny, playable multiplayer demo. The long-term target is an engine
capable of rendering beautiful games (including classic-style aesthetics) with strong multiplayer
and co-op support.

## Product Goals

- **Visual quality**: clean lighting, strong material response, and stable frame pacing.
- **Playable**: input latency, camera control, and interaction feel good.
- **Traversal**: players can walk, drive cars, and fly aircraft on-planet and between planets.
- **Multiplayer**: authoritative server, client prediction, snapshot interpolation.
- **Data-driven**: assets and world data are externalized and reloadable.
- **Tools**: debugging, profiling, and validation are first-class.
- **True cross-platform**: one shared architecture targeting desktop, mobile (Android/iOS), and consoles.

## Non-Goals (For Now)

- Massive open worlds or MMO-scale networking.
- Full editor suite (focus on runtime first).

## Near-Term Milestone: Tiny Multiplayer Demo

The demo should be a small co-op scene that demonstrates:
- client/server connection
- replicated player movement
- a small shared world state (e.g., a few blocks/entities)
- stable frame pacing and render loop
- on-foot traversal baseline first; vehicle/aircraft traversal is intentionally deferred until protocol/runtime hardening is complete

Success criteria: two clients connect to a server, move around, and see each other consistently,
with graceful disconnects and recoveries.
