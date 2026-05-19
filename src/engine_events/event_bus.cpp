#include "engine_events/event_bus.hpp"

#include <algorithm>
#include <utility>

EventBus::EventBus() = default;

EventBus::~EventBus() = default;

void EventBus::reserve(size_t subscriber_capacity, size_t fixed_event_capacity,
                       size_t frame_event_capacity) {
    subscribers_.reserve(subscriber_capacity);
    fixed_events_.reserve(fixed_event_capacity);
    frame_events_.reserve(frame_event_capacity);
    dispatch_ids_.reserve(subscriber_capacity);
    fixed_event_capacity_ = fixed_event_capacity;
    frame_event_capacity_ = frame_event_capacity;
}

void EventBus::clear() {
    subscribers_.clear();
    fixed_events_.clear();
    frame_events_.clear();
    dispatch_ids_.clear();
    dropped_events_ = 0;
    next_subscription_id_ = 1;
    dispatch_depth_ = 0;
}

void EventBus::unsubscribe(EventSubscriptionId id) {
    Subscriber *subscriber = find_subscriber(id);
    if (subscriber == nullptr) {
        return;
    }

    if (dispatch_depth_ > 0) {
        subscriber->active = false;
        return;
    }

    subscribers_.erase(std::remove_if(subscribers_.begin(), subscribers_.end(),
                                      [id](const Subscriber &entry) {
                                          return entry.id == id;
                                      }),
                       subscribers_.end());
}

size_t EventBus::drain_fixed(const EventContext &context) {
    return drain(fixed_events_, context, EventPhase::Fixed);
}

size_t EventBus::drain_frame(const EventContext &context) {
    return drain(frame_events_, context, EventPhase::Frame);
}

size_t EventBus::dropped_events() const {
    return dropped_events_;
}

size_t EventBus::fixed_queue_size() const {
    return fixed_events_.size();
}

size_t EventBus::frame_queue_size() const {
    return frame_events_.size();
}

size_t EventBus::subscriber_count() const {
    return static_cast<size_t>(
        std::count_if(subscribers_.begin(), subscribers_.end(),
                      [](const Subscriber &entry) { return entry.active; }));
}

void EventBus::dispatch(const Event &event, const EventContext &context,
                        EventPhase phase) {
    std::vector<EventSubscriptionId> nested_dispatch_ids;
    const bool restore_dispatch_ids = dispatch_depth_ > 0;
    if (restore_dispatch_ids) {
        nested_dispatch_ids.swap(dispatch_ids_);
    }

    dispatch_ids_.clear();
    const size_t event_type = event.index();
    for (const Subscriber &subscriber : subscribers_) {
        if (subscriber.active && subscriber.phase == phase &&
            subscriber.type_index == event_type) {
            dispatch_ids_.push_back(subscriber.id);
        }
    }

    ++dispatch_depth_;
    for (EventSubscriptionId id : dispatch_ids_) {
        Subscriber *subscriber = find_subscriber(id);
        if (subscriber != nullptr && subscriber->active) {
            subscriber->handler(event, context);
        }
    }
    --dispatch_depth_;

    if (dispatch_depth_ == 0) {
        compact_subscribers();
    }

    if (restore_dispatch_ids) {
        dispatch_ids_.swap(nested_dispatch_ids);
    }
}

size_t EventBus::drain(std::vector<Event> &queue, const EventContext &context,
                       EventPhase phase) {
    const size_t count = queue.size();
    std::vector<Event> draining;
    draining.swap(queue);
    queue.reserve(draining.capacity());

    EventContext drain_context = context;
    drain_context.phase = phase;
    for (const Event &event : draining) {
        dispatch(event, drain_context, phase);
    }

    return count;
}

EventBus::Subscriber *EventBus::find_subscriber(EventSubscriptionId id) {
    for (Subscriber &subscriber : subscribers_) {
        if (subscriber.id == id) {
            return &subscriber;
        }
    }
    return nullptr;
}

const EventBus::Subscriber *
EventBus::find_subscriber(EventSubscriptionId id) const {
    for (const Subscriber &subscriber : subscribers_) {
        if (subscriber.id == id) {
            return &subscriber;
        }
    }
    return nullptr;
}

void EventBus::compact_subscribers() {
    subscribers_.erase(
        std::remove_if(subscribers_.begin(), subscribers_.end(),
                       [](const Subscriber &entry) { return !entry.active; }),
        subscribers_.end());
}
