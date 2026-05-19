#include "engine_events/event_bus.hpp"

#include <cassert>
#include <vector>

namespace {

EventContext fixed_context(uint64_t tick = 7) {
    EventContext context{};
    context.phase = EventPhase::Fixed;
    context.tick = tick;
    context.dt = 1.0f / 60.0f;
    return context;
}

EventContext frame_context(uint64_t frame = 3) {
    EventContext context{};
    context.phase = EventPhase::Frame;
    context.frame = frame;
    context.dt = 1.0f / 30.0f;
    context.alpha = 0.5f;
    return context;
}

void subscribe_and_receive_fixed_event() {
    EventBus bus;
    int received = 0;
    bus.subscribe<PlayerJumpedEvent>(
        EventPhase::Fixed,
        [&](const PlayerJumpedEvent &event, const EventContext &context) {
            assert(context.phase == EventPhase::Fixed);
            assert(context.tick == 12);
            received = static_cast<int>(event.player_id);
        });

    assert(bus.enqueue_fixed(PlayerJumpedEvent{42, glm::vec3(1.0f)}));
    assert(bus.drain_fixed(fixed_context(12)) == 1);
    assert(received == 42);
}

void subscribe_and_receive_frame_event() {
    EventBus bus;
    int received = 0;
    bus.subscribe<HudMessageEvent>(
        EventPhase::Frame,
        [&](const HudMessageEvent &event, const EventContext &context) {
            assert(context.phase == EventPhase::Frame);
            assert(context.frame == 8);
            received = static_cast<int>(event.message_id);
        });

    assert(bus.enqueue_frame(HudMessageEvent{33, 1.5f}));
    assert(bus.drain_frame(frame_context(8)) == 1);
    assert(received == 33);
}

void delivery_order_is_preserved() {
    EventBus bus;
    std::vector<uint32_t> received;
    bus.subscribe<PlayerMovedEvent>(
        EventPhase::Fixed,
        [&](const PlayerMovedEvent &event, const EventContext &) {
            received.push_back(event.player_id);
        });

    assert(bus.enqueue_fixed(PlayerMovedEvent{1, {}, {}}));
    assert(bus.enqueue_fixed(PlayerMovedEvent{2, {}, {}}));
    assert(bus.enqueue_fixed(PlayerMovedEvent{3, {}, {}}));
    bus.drain_fixed(fixed_context());

    assert((received == std::vector<uint32_t>{1, 2, 3}));
}

void fixed_and_frame_queues_are_separate() {
    EventBus bus;
    int fixed_received = 0;
    int frame_received = 0;
    bus.subscribe<PlayerJumpedEvent>(
        EventPhase::Fixed,
        [&](const PlayerJumpedEvent &, const EventContext &) {
            ++fixed_received;
        });
    bus.subscribe<PlayerJumpedEvent>(
        EventPhase::Frame,
        [&](const PlayerJumpedEvent &, const EventContext &) {
            ++frame_received;
        });

    assert(bus.enqueue_fixed(PlayerJumpedEvent{1, {}}));
    assert(bus.enqueue_frame(PlayerJumpedEvent{2, {}}));
    bus.drain_fixed(fixed_context());

    assert(fixed_received == 1);
    assert(frame_received == 0);
    assert(bus.fixed_queue_size() == 0);
    assert(bus.frame_queue_size() == 1);

    bus.drain_frame(frame_context());
    assert(frame_received == 1);
}

void unsubscribe_stops_delivery() {
    EventBus bus;
    int received = 0;
    const EventSubscriptionId id = bus.subscribe<PlayerJumpedEvent>(
        EventPhase::Fixed,
        [&](const PlayerJumpedEvent &, const EventContext &) {
            ++received;
        });

    bus.unsubscribe(id);
    assert(bus.enqueue_fixed(PlayerJumpedEvent{1, {}}));
    bus.drain_fixed(fixed_context());
    assert(received == 0);
    assert(bus.subscriber_count() == 0);
}

void emit_immediate_works_synchronously() {
    EventBus bus;
    int received = 0;
    bus.subscribe<PlayerSpawnedEvent>(
        EventPhase::Frame,
        [&](const PlayerSpawnedEvent &event, const EventContext &) {
            received = static_cast<int>(event.player_id);
        });

    bus.emit_immediate(EventPhase::Frame, PlayerSpawnedEvent{9, {}},
                       frame_context());
    assert(received == 9);
    assert(bus.frame_queue_size() == 0);
}

void enqueue_during_drain_is_deferred() {
    EventBus bus;
    int received = 0;
    bus.subscribe<PlayerJumpedEvent>(
        EventPhase::Fixed,
        [&](const PlayerJumpedEvent &event, const EventContext &) {
            ++received;
            if (event.player_id == 1) {
                assert(bus.enqueue_fixed(PlayerJumpedEvent{2, {}}));
            }
        });

    assert(bus.enqueue_fixed(PlayerJumpedEvent{1, {}}));
    assert(bus.drain_fixed(fixed_context()) == 1);
    assert(received == 1);
    assert(bus.fixed_queue_size() == 1);

    assert(bus.drain_fixed(fixed_context()) == 1);
    assert(received == 2);
}

void overflow_reports_dropped_events() {
    EventBus bus;
    bus.reserve(1, 2, 1);

    assert(bus.enqueue_fixed(PlayerJumpedEvent{1, {}}));
    assert(bus.enqueue_fixed(PlayerJumpedEvent{2, {}}));
    assert(!bus.enqueue_fixed(PlayerJumpedEvent{3, {}}));
    assert(bus.dropped_events() == 1);
    assert(bus.fixed_queue_size() == 2);
}

void event_context_is_passed_correctly() {
    EventBus bus;
    bool received = false;
    bus.subscribe<FrameStartedEvent>(
        EventPhase::Frame,
        [&](const FrameStartedEvent &, const EventContext &context) {
            assert(context.phase == EventPhase::Frame);
            assert(context.frame == 99);
            assert(context.dt == 0.25f);
            assert(context.alpha == 0.75f);
            received = true;
        });

    EventContext context = frame_context(99);
    context.dt = 0.25f;
    context.alpha = 0.75f;
    assert(bus.enqueue_frame(FrameStartedEvent{99, 0.25f, 0.75f}));
    bus.drain_frame(context);
    assert(received);
}

void multiple_subscribers_receive_same_event() {
    EventBus bus;
    int received_a = 0;
    int received_b = 0;
    bus.subscribe<VehicleDestroyedEvent>(
        EventPhase::Fixed,
        [&](const VehicleDestroyedEvent &, const EventContext &) {
            ++received_a;
        });
    bus.subscribe<VehicleDestroyedEvent>(
        EventPhase::Fixed,
        [&](const VehicleDestroyedEvent &, const EventContext &) {
            ++received_b;
        });

    assert(bus.enqueue_fixed(VehicleDestroyedEvent{5}));
    bus.drain_fixed(fixed_context());
    assert(received_a == 1);
    assert(received_b == 1);
}

void unsubscribe_during_dispatch_is_safe() {
    EventBus bus;
    int received_a = 0;
    int received_b = 0;
    EventSubscriptionId id_b = 0;

    bus.subscribe<PlayerJumpedEvent>(
        EventPhase::Fixed,
        [&](const PlayerJumpedEvent &, const EventContext &) {
            ++received_a;
            bus.unsubscribe(id_b);
        });
    id_b = bus.subscribe<PlayerJumpedEvent>(
        EventPhase::Fixed,
        [&](const PlayerJumpedEvent &, const EventContext &) {
            ++received_b;
        });

    assert(bus.enqueue_fixed(PlayerJumpedEvent{1, {}}));
    bus.drain_fixed(fixed_context());
    assert(received_a == 1);
    assert(received_b == 0);
    assert(bus.subscriber_count() == 1);
}

} // namespace

int main() {
    subscribe_and_receive_fixed_event();
    subscribe_and_receive_frame_event();
    delivery_order_is_preserved();
    fixed_and_frame_queues_are_separate();
    unsubscribe_stops_delivery();
    emit_immediate_works_synchronously();
    enqueue_during_drain_is_deferred();
    overflow_reports_dropped_events();
    event_context_is_passed_correctly();
    multiple_subscribers_receive_same_event();
    unsubscribe_during_dispatch_is_safe();
    return 0;
}
