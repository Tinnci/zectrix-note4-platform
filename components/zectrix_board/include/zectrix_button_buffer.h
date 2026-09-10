#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

enum class ZectrixButton : uint8_t { kUp = 0, kDown, kOk };
enum class ZectrixButtonAction : uint8_t { kClick = 0, kLongPress, kWake };

struct ZectrixButtonEvent {
    ZectrixButton button = ZectrixButton::kOk;
    ZectrixButtonAction action = ZectrixButtonAction::kClick;
};

// The board serializes this bounded buffer with a short critical section.
// Wake notifications live separately and never consume physical input slots.
class ZectrixButtonBuffer {
public:
    static constexpr std::size_t kCapacity = 16;

    bool Push(const ZectrixButtonEvent& event) {
        if (event.action == ZectrixButtonAction::kWake) return false;
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
            if (remove == size_ && event.action == ZectrixButtonAction::kLongPress) {
                for (std::size_t i = size_; i > 0; --i) {
                    if (events_[i - 1].action == ZectrixButtonAction::kClick) {
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

    bool Pop(ZectrixButtonEvent* event) {
        if (event == nullptr || size_ == 0) return false;
        *event = events_[0];
        Erase(0);
        return true;
    }

private:
    static bool IsDirection(const ZectrixButtonEvent& event) {
        return event.action == ZectrixButtonAction::kClick &&
               event.button != ZectrixButton::kOk;
    }
    static bool IsShutdown(const ZectrixButtonEvent& event) {
        return event.button == ZectrixButton::kDown &&
               event.action == ZectrixButtonAction::kLongPress;
    }
    void Erase(std::size_t index) {
        for (std::size_t i = index + 1; i < size_; ++i) events_[i - 1] = events_[i];
        --size_;
    }

    std::array<ZectrixButtonEvent, kCapacity> events_{};
    std::size_t size_ = 0;
};
