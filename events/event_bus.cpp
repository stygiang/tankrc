#include "event_bus.h"

#include <array>
#include <cstddef>

namespace TankRC::Events {
namespace {
constexpr std::size_t kQueueSize = 16;
constexpr std::size_t kMaxHandlers = 8;

std::array<Event, kQueueSize> queue{};
std::size_t head = 0;
std::size_t tail = 0;

std::array<EventHandler, kMaxHandlers> handlers{};
std::size_t handlerCount = 0;

bool queueEmpty() {
    return head == tail;
}

bool queueFull() {
    return ((tail + 1) % kQueueSize) == head;
}

void enqueue(const Event& event) {
    if (queueFull()) {
        head = (head + 1) % kQueueSize;
    }
    queue[tail] = event;
    tail = (tail + 1) % kQueueSize;
}

bool dequeue(Event& event) {
    if (queueEmpty()) {
        return false;
    }
    event = queue[head];
    head = (head + 1) % kQueueSize;
    return true;
}
}  // namespace

void subscribe(EventHandler handler) {
    if (!handler) {
        return;
    }
    for (std::size_t i = 0; i < handlerCount; ++i) {
        if (handlers[i] == handler) {
            return;
        }
    }
    if (handlerCount < kMaxHandlers) {
        handlers[handlerCount++] = handler;
    }
}

void publish(const Event& event) {
    enqueue(event);
}

void process() {
    Event event{};
    while (dequeue(event)) {
        for (std::size_t i = 0; i < handlerCount; ++i) {
            if (handlers[i]) {
                handlers[i](event);
            }
        }
    }
}

void clear() {
    head = tail = 0;
}
}  // namespace TankRC::Events
