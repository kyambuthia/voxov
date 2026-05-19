#pragma once

#include "engine_events/event_types.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <type_traits>
#include <utility>
#include <vector>

enum class EventPhase : uint8_t {
    Fixed,
    Frame,
};

struct EventContext {
    EventPhase phase = EventPhase::Fixed;
    uint64_t tick = 0;
    uint64_t frame = 0;
    float dt = 0.0f;
    float alpha = 0.0f;
};

using EventSubscriptionId = uint64_t;

class EventBus {
public:
    EventBus();
    ~EventBus();

    void reserve(size_t subscriber_capacity, size_t fixed_event_capacity,
                 size_t frame_event_capacity);
    void clear();

    template <typename EventT>
    bool enqueue_fixed(const EventT &event);

    template <typename EventT>
    bool enqueue_frame(const EventT &event);

    template <typename EventT, typename HandlerT>
    EventSubscriptionId subscribe(EventPhase phase, HandlerT &&handler);

    void unsubscribe(EventSubscriptionId id);

    template <typename EventT>
    void emit_immediate(EventPhase phase, const EventT &event,
                        const EventContext &context);

    size_t drain_fixed(const EventContext &context);
    size_t drain_frame(const EventContext &context);

    size_t dropped_events() const;
    size_t fixed_queue_size() const;
    size_t frame_queue_size() const;
    size_t subscriber_count() const;

private:
    struct Subscriber {
        size_t type_index = 0;
        EventPhase phase = EventPhase::Fixed;
        EventSubscriptionId id = 0;
        bool active = true;
        std::function<void(const Event &, const EventContext &)> handler;
    };

    template <typename EventT>
    static size_t event_type_index();

    template <typename EventT>
    bool enqueue(std::vector<Event> &queue, size_t capacity,
                 const EventT &event);

    void dispatch(const Event &event, const EventContext &context,
                  EventPhase phase);
    size_t drain(std::vector<Event> &queue, const EventContext &context,
                 EventPhase phase);
    Subscriber *find_subscriber(EventSubscriptionId id);
    const Subscriber *find_subscriber(EventSubscriptionId id) const;
    void compact_subscribers();

    std::vector<Subscriber> subscribers_;
    std::vector<Event> fixed_events_;
    std::vector<Event> frame_events_;
    std::vector<EventSubscriptionId> dispatch_ids_;
    size_t fixed_event_capacity_ = 1024;
    size_t frame_event_capacity_ = 1024;
    size_t dropped_events_ = 0;
    EventSubscriptionId next_subscription_id_ = 1;
    uint32_t dispatch_depth_ = 0;
};

template <typename EventT>
bool EventBus::enqueue_fixed(const EventT &event) {
    return enqueue(fixed_events_, fixed_event_capacity_, event);
}

template <typename EventT>
bool EventBus::enqueue_frame(const EventT &event) {
    return enqueue(frame_events_, frame_event_capacity_, event);
}

template <typename EventT, typename HandlerT>
EventSubscriptionId EventBus::subscribe(EventPhase phase, HandlerT &&handler) {
    const EventSubscriptionId id = next_subscription_id_++;
    subscribers_.push_back(Subscriber{
        event_type_index<EventT>(),
        phase,
        id,
        true,
        [handler = std::forward<HandlerT>(handler)](
            const Event &event, const EventContext &context) mutable {
            handler(std::get<EventT>(event), context);
        },
    });
    return id;
}

template <typename EventT>
void EventBus::emit_immediate(EventPhase phase, const EventT &event,
                              const EventContext &context) {
    dispatch(Event{event}, context, phase);
}

template <typename EventT>
size_t EventBus::event_type_index() {
    using PlainEventT = std::remove_cvref_t<EventT>;
    static_assert(std::is_constructible_v<Event, PlainEventT>,
                  "EventT must be an Event variant alternative");
    return Event{PlainEventT{}}.index();
}

template <typename EventT>
bool EventBus::enqueue(std::vector<Event> &queue, size_t capacity,
                       const EventT &event) {
    static_assert(std::is_constructible_v<Event, std::remove_cvref_t<EventT>>,
                  "EventT must be an Event variant alternative");
    if (queue.size() >= capacity) {
        ++dropped_events_;
        return false;
    }
    queue.push_back(Event{event});
    return true;
}
