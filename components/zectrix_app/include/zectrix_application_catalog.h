#pragma once

#include "zectrix/sdk/application.h"

namespace zectrix::app {

// Composition finishes before the runtime borrows this storage. Entry zero is
// the Launcher; all remaining entries supply both menu labels and open targets.
class ApplicationCatalog {
public:
    static constexpr std::size_t kCapacity = 16;

    bool Add(const char* id, const char* label, sdk::ApplicationFactory& factory) {
        if (size_ == entries_.size()) return false;
        entries_[size_++] = {id, label, &factory};
        return true;
    }
    const sdk::ApplicationDescriptor* data() const { return entries_.data(); }
    std::size_t size() const { return size_; }
    std::size_t menu_size() const { return size_ ? size_ - 1 : 0; }
    const sdk::ApplicationDescriptor* MenuAt(std::size_t index) const {
        return index < menu_size() ? &entries_[index + 1] : nullptr;
    }

private:
    std::array<sdk::ApplicationDescriptor, kCapacity> entries_{};
    std::size_t size_ = 0;
};

}  // namespace zectrix::app
