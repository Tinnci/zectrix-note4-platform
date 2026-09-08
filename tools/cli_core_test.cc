#include <algorithm>
#include <cassert>
#include <cstring>
#include <deque>
#include <string>

#include "zectrix_cli_core.h"
#include "zectrix_cli_session.h"

using namespace zectrix::cli;

namespace {

class FakeTransport final : public CliTransport {
public:
    bool IsConnected() const override { return connected; }

    std::size_t Read(uint8_t* destination, std::size_t capacity) override {
        const std::size_t count = std::min({capacity, input.size(), read_size});
        for (std::size_t index = 0; index < count; ++index) {
            destination[index] = input.front();
            input.pop_front();
        }
        if (over_report_read) return capacity + 7;
        return count;
    }

    bool Write(const char* data, std::size_t size) override {
        if (fail_write) return false;
        output.append(data, size);
        return true;
    }

    void DiscardInput() override { input.clear(); }

    void Send(const std::string& text) {
        for (const unsigned char value : text) input.push_back(value);
    }

    bool connected = false;
    bool fail_write = false;
    bool over_report_read = false;
    std::size_t read_size = 32;
    std::deque<uint8_t> input;
    std::string output;
};

class RecordingExecutor final : public CliExecutor {
public:
    ExecuteStatus Execute(const Invocation& invocation,
                          BoundedOutput* output) override {
        assert(!executing);
        executing = true;
        ++calls;
        last.clear();
        for (std::size_t index = 0; index < invocation.count; ++index) {
            if (!last.empty()) last += '|';
            last += invocation[index];
        }
        if (std::strcmp(invocation[0], "known") != 0) {
            executing = false;
            return ExecuteStatus::kUnknownCommand;
        }
        output->Append("done");
        executing = false;
        return ExecuteStatus::kOk;
    }

    std::size_t calls = 0;
    std::string last;
    bool executing = false;
};

void Drain(CliSession& session, FakeTransport& transport) {
    while (!transport.input.empty()) session.Poll();
}

void TestSession() {
    FakeTransport transport;
    RecordingExecutor executor;
    CliSession session(transport, executor);

    session.Poll();
    assert(!session.connected());
    transport.connected = true;
    session.Poll();
    assert(session.connected());
    assert(transport.output.find("Zectrix maintenance CLI\r\nzectrix> ") !=
           std::string::npos);

    transport.Send("known one\r\n");
    Drain(session, transport);
    assert(executor.calls == 1);
    assert(executor.last == "known|one");
    assert(transport.output.find("done\r\nzectrix> ") != std::string::npos);
    assert(session.history_size() == 1);

    transport.Send("\"bad\r");
    Drain(session, transport);
    assert(executor.calls == 1);
    assert(transport.output.find("error: unterminated quote\r\nzectrix> ") !=
           std::string::npos);

    transport.Send("discard me\x03known\r");
    Drain(session, transport);
    assert(executor.calls == 2);
    assert(executor.last == "known");
    assert(transport.output.find("^C\r\nzectrix> ") != std::string::npos);

    // Recall the newest command and submit it. History remains RAM-only and
    // duplicate adjacent commands do not consume another slot.
    transport.Send("\x1b[A\r");
    Drain(session, transport);
    assert(executor.calls == 3);
    assert(executor.last == "known");
    assert(session.history_size() == 2);

    // Cursor-left insertion is handled by the bounded editor.
    transport.Send("knwn\x1b[D\x1b[Do\r");
    Drain(session, transport);
    assert(executor.calls == 4);
    assert(executor.last == "known");

    // An overlong line is rejected as a whole and never enters history.
    transport.Send(std::string(kMaximumLineSize + 4, 'x') + "\r");
    Drain(session, transport);
    assert(session.line_size() == 0);
    assert(transport.output.find('\a') != std::string::npos);
    assert(executor.calls == 4);
    assert(transport.output.find("error: line too long") != std::string::npos);

    for (int index = 0; index < 10; ++index) {
        transport.Send("known " + std::to_string(index) + "\r");
        Drain(session, transport);
    }
    assert(session.history_size() == kHistoryEntries);

    // A broken transport cannot make the session read past its fixed buffer.
    transport.over_report_read = true;
    transport.Send("known\r");
    session.Poll();
    transport.over_report_read = false;

    transport.connected = false;
    session.Poll();
    assert(!session.connected());
    transport.connected = true;
    session.Poll();
    assert(session.connected());
    assert(session.line_size() == 0);

    transport.fail_write = true;
    transport.Send("x");
    session.Poll();
    assert(!session.connected());
}

void TestTerminalInputBoundaries() {
    for (const std::size_t chunk : {1U, 7U, 32U}) {
        FakeTransport transport;
        transport.connected = true;
        transport.read_size = chunk;
        RecordingExecutor executor;
        CliSession session(transport, executor);
        session.Poll();

        // USB packet boundaries must not affect editing or CRLF folding.
        for (const std::string command : {
                 "knXown\x1b[H\x1b[2C\x1b[3~\r\n",
                 "knownX\x1b[1~\x1b[999C\x08\x1b[4~\r\n",
                 "nown\x1bOHk\x1bOF\r\n",
                 "kn\x1b[1;5Cown\x1b[?25h\r\n",
                 "\x1b[200~known\x1b[201~\r\n"}) {
            const auto calls = executor.calls;
            transport.Send(command);
            Drain(session, transport);
            assert(executor.calls == calls + 1 && executor.last == "known");
        }
        transport.Send("known\t\"two words\"\r\n");
        Drain(session, transport);
        assert(executor.last == "known|two words");

        const auto calls = executor.calls;
        const auto history = session.history_size();
        // Truncation used to turn this rejected line into a valid command.
        const std::string prefix = "known" + std::string(kMaximumLineSize - 5, ' ');
        for (const auto& command : {
                 prefix + "unexpected\r\n",
                 std::string("known\0unexpected\r\n", 18),
                 std::string("known\x1b[") + std::string(80, '1') + "~\r\n",
                 std::string("known\x1b[\r\n"),
                 std::string("known\x1b]0;title\x07\r\n")}) {
            transport.Send(command);
            Drain(session, transport);
            assert(executor.calls == calls);
            assert(session.history_size() == history && session.line_size() == 0);
        }
        assert(transport.output.find("error: invalid input") != std::string::npos);
        assert(transport.output.find("error: invalid escape") != std::string::npos);
        transport.Send(prefix + "overflow\x03known\r\n");
        Drain(session, transport);
        assert(executor.calls == calls + 1 && executor.last == "known");
    }
}

}  // namespace

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
    const char binary[] = "version\0unexpected";
    assert(ParseLine(binary, sizeof(binary) - 1, &invocation) ==
           ParseStatus::kInvalidArgument);
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

    TestSession();
    TestTerminalInputBoundaries();
}
