#include "zectrix_cli_core.h"

#include <cstring>

namespace zectrix::cli {
namespace {

bool IsSpace(char value) {
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

const CommandDescriptor* Find(const CommandDescriptor* commands,
                              std::size_t count, const char* name) {
    if (commands == nullptr || name == nullptr) return nullptr;
    for (std::size_t index = 0; index < count; ++index) {
        if (commands[index].name != nullptr &&
            std::strcmp(commands[index].name, name) == 0) {
            return &commands[index];
        }
    }
    return nullptr;
}

}  // namespace

ParseStatus ParseLine(const char* line, std::size_t size,
                      Invocation* invocation) {
    if (line == nullptr || invocation == nullptr) {
        return ParseStatus::kInvalidArgument;
    }
    *invocation = {};
    if (size > kMaximumLineSize) return ParseStatus::kLineTooLong;

    std::size_t input = 0;
    while (input < size) {
        while (input < size && IsSpace(line[input])) ++input;
        if (input == size || line[input] == '\0') break;
        if (invocation->count == kMaximumArguments) {
            return ParseStatus::kTooManyArguments;
        }

        auto& token = invocation->arguments[invocation->count];
        std::size_t output = 0;
        char quote = 0;
        bool started = false;
        while (input < size && line[input] != '\0') {
            const char value = line[input];
            if (quote == 0 && IsSpace(value)) break;
            started = true;
            if (value == '\\') {
                ++input;
                if (input == size || line[input] == '\0') {
                    return ParseStatus::kInvalidEscape;
                }
                if (output == kMaximumTokenSize) {
                    return ParseStatus::kTokenTooLong;
                }
                token[output++] = line[input++];
                continue;
            }
            if (value == '\'' || value == '"') {
                if (quote == 0) {
                    quote = value;
                    ++input;
                    continue;
                }
                if (quote == value) {
                    quote = 0;
                    ++input;
                    continue;
                }
            }
            if (output == kMaximumTokenSize) {
                return ParseStatus::kTokenTooLong;
            }
            token[output++] = value;
            ++input;
        }
        if (quote != 0) return ParseStatus::kUnterminatedQuote;
        if (started) {
            token[output] = '\0';
            ++invocation->count;
        }
    }
    return invocation->count == 0 ? ParseStatus::kEmpty : ParseStatus::kOk;
}

ResolveStatus Resolve(const CommandDescriptor* roots, std::size_t root_count,
                      const Invocation& invocation, Resolution* resolution) {
    if (roots == nullptr || root_count == 0 || resolution == nullptr) {
        return ResolveStatus::kInvalidArgument;
    }
    *resolution = {};
    if (invocation.count == 0) return ResolveStatus::kUnknownCommand;

    const CommandDescriptor* current = Find(roots, root_count, invocation[0]);
    if (current == nullptr) return ResolveStatus::kUnknownCommand;
    std::size_t consumed = 1;
    while (consumed < invocation.count && current->child_count != 0) {
        if (consumed == kMaximumCommandDepth) {
            return ResolveStatus::kDepthExceeded;
        }
        const CommandDescriptor* child =
            Find(current->children, current->child_count, invocation[consumed]);
        if (child == nullptr) break;
        current = child;
        ++consumed;
    }
    if (!current->executable) {
        return ResolveStatus::kIncompleteCommand;
    }
    resolution->command = current;
    resolution->argument_index = consumed;
    return ResolveStatus::kOk;
}

bool BoundedOutput::Append(const char* text) {
    if (text == nullptr) return false;
    bool complete = true;
    while (*text != '\0') {
        if (!Append(*text++)) complete = false;
    }
    return complete;
}

bool BoundedOutput::Append(char value) {
    if (size_ == kMaximumOutputSize) {
        truncated_ = true;
        return false;
    }
    data_[size_++] = value;
    data_[size_] = '\0';
    return true;
}

void BoundedOutput::Clear() {
    data_ = {};
    size_ = 0;
    truncated_ = false;
}

}  // namespace zectrix::cli
