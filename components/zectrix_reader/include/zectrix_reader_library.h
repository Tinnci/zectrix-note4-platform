#pragma once

#include "zectrix_reader.h"

namespace zectrix::reader {

struct BookInfo {
    std::array<char, 64> id{};
    uint32_t bytes = 0;
    Format format = Format::Text;
};

class Library {
public:
    virtual ~Library() = default;
    virtual Result Refresh() = 0;
    virtual std::size_t count() const = 0;
    virtual BookInfo Get(std::size_t index) const = 0;
    virtual bool truncated() const = 0;
    // The library owns one source until Close, Refresh or its destruction.
    virtual Result Open(std::size_t index, Source** source) = 0;
    virtual void Close() = 0;
};

}  // namespace zectrix::reader
