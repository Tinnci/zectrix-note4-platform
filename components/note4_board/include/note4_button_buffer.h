#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

enum class Note4Button : uint8_t { kUp = 0, kDown, kOk };
enum class Note4ButtonAction : uint8_t { kClick = 0, kLongPress, kWake };

struct Note4ButtonEvent {
    Note4Button button = Note4Button::kOk;
    Note4ButtonAction action = Note4ButtonAction::kClick;
};

// The board serializes this bounded buffer with a short critical section.
// Wake notifications live separately and never consume physical input slots.
class Note4ButtonBuffer {
public:
    static constexpr std::size_t kCapacity = 16;

    bool Push(const Note4ButtonEvent& event) {
        if (event.action == Note4ButtonAction::kWake) return false;
        if (IsShutdown(event)) {
            size_ = 1;
            events_[0] = event;
            return true;
        }
        if (size_ != 0 && IsShutdown(events_[0])) return false;
        if (size_ == kCapacity) {
            if (IsDirection(event)) return false;
            std::size_t remove = size_;
            // Preserve retained order, sacrificing the newest navigation first.
            for (std::size_t i = size_; i > 0; --i) {
                if (IsDirection(events_[i - 1])) { remove = i - 1; break; }
            }
            if (remove == size_ && event.action == Note4ButtonAction::kLongPress) {
                for (std::size_t i = size_; i > 0; --i) {
                    if (events_[i - 1].action == Note4ButtonAction::kClick) {
                        remove = i - 1;
                        break;
                    }
                }
            }
            // Repeated Back is already retained if every slot contains Back.
            if (remove == size_) return false;
            Erase(remove);
        }
        events_[size_++] = event;
        return true;
    }

    bool Pop(Note4ButtonEvent* event) {
        if (event == nullptr || size_ == 0) return false;
        *event = events_[0];
        Erase(0);
        return true;
    }

private:
    static bool IsDirection(const Note4ButtonEvent& event) {
        return event.action == Note4ButtonAction::kClick &&
               event.button != Note4Button::kOk;
    }
    static bool IsShutdown(const Note4ButtonEvent& event) {
        return event.button == Note4Button::kDown &&
               event.action == Note4ButtonAction::kLongPress;
    }
    void Erase(std::size_t index) {
        for (std::size_t i = index + 1; i < size_; ++i) events_[i - 1] = events_[i];
        --size_;
    }

    std::array<Note4ButtonEvent, kCapacity> events_{};
    std::size_t size_ = 0;
};
