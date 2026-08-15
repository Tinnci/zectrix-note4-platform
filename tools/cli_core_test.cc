#include <cassert>
#include <cstring>
#include <string>

#include "zectrix_cli_core.h"

using namespace zectrix::cli;

int main() {
    Invocation invocation{};
    assert(ParseLine("system info", 11, &invocation) == ParseStatus::kOk);
    assert(invocation.count == 2);
    assert(std::strcmp(invocation[0], "system") == 0);
    assert(std::strcmp(invocation[1], "info") == 0);

    const char quoted[] = "time set \"2026-08-15 12:00\" escaped\\ value";
    assert(ParseLine(quoted, sizeof(quoted) - 1, &invocation) ==
           ParseStatus::kOk);
    assert(invocation.count == 4);
    assert(std::strcmp(invocation[2], "2026-08-15 12:00") == 0);
    assert(std::strcmp(invocation[3], "escaped value") == 0);

    assert(ParseLine("\t  ", 3, &invocation) == ParseStatus::kEmpty);
    assert(ParseLine("\"open", 5, &invocation) ==
           ParseStatus::kUnterminatedQuote);
    assert(ParseLine("bad\\", 4, &invocation) == ParseStatus::kInvalidEscape);
    const std::string long_line(kMaximumLineSize + 1, 'x');
    assert(ParseLine(long_line.data(), long_line.size(), &invocation) ==
           ParseStatus::kLineTooLong);
    const std::string long_token(kMaximumTokenSize + 1, 'x');
    assert(ParseLine(long_token.data(), long_token.size(), &invocation) ==
           ParseStatus::kTokenTooLong);

    static constexpr CommandDescriptor system_children[] = {
        {"info", "Show system identity", "system info", Access::kReadOnly,
         Execution::kOwnerRequest, true, nullptr, 0},
        {"heap", "Show heap state", "system heap", Access::kReadOnly,
         Execution::kOwnerRequest, true, nullptr, 0},
    };
    static constexpr CommandDescriptor roots[] = {
        {"system", "System commands", "system <command>", Access::kReadOnly,
         Execution::kImmediate, false, system_children, 2},
        {"version", "Show CLI version", "version", Access::kReadOnly,
         Execution::kImmediate, true, nullptr, 0},
    };
    assert(ParseLine("system info extra", 17, &invocation) == ParseStatus::kOk);
    Resolution resolution{};
    assert(Resolve(roots, 2, invocation, &resolution) == ResolveStatus::kOk);
    assert(resolution.command == &system_children[0]);
    assert(resolution.argument_index == 2);
    assert(std::strcmp(invocation[resolution.argument_index], "extra") == 0);
    assert(ParseLine("system", 6, &invocation) == ParseStatus::kOk);
    assert(Resolve(roots, 2, invocation, &resolution) ==
           ResolveStatus::kIncompleteCommand);
    assert(ParseLine("missing", 7, &invocation) == ParseStatus::kOk);
    assert(Resolve(roots, 2, invocation, &resolution) ==
           ResolveStatus::kUnknownCommand);

    BoundedOutput output;
    assert(output.Append("ready"));
    assert(std::strcmp(output.data(), "ready") == 0);
    for (std::size_t index = output.size(); index < kMaximumOutputSize; ++index) {
        assert(output.Append('x'));
    }
    assert(!output.Append('!'));
    assert(output.truncated());
    output.Clear();
    assert(output.size() == 0 && !output.truncated());

    CancellationToken cancellation;
    assert(!cancellation.IsCancelled());
    cancellation.Cancel();
    assert(cancellation.IsCancelled());
    cancellation.Reset();
    assert(!cancellation.IsCancelled());
}
