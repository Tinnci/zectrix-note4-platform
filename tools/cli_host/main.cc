#include "simulated_platform.h"
#include "stdio_transport.h"
#include "zectrix_cli_diagnostics.h"

#include <atomic>
#include <charconv>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <exception>

namespace {

// Process signals can reach the owner thread as well as the terminal thread.
std::atomic<int> stopped_signal{0};
static_assert(std::atomic<int>::is_always_lock_free);
void OnSignal(int signal) { stopped_signal.store(signal, std::memory_order_relaxed); }

class Signals final {
public:
    bool Open() {
        for (const int signal : numbers_) {
            struct sigaction action{};
            action.sa_handler = signal == SIGPIPE ? SIG_IGN : OnSignal;
            sigemptyset(&action.sa_mask);
            if (sigaction(signal, &action, &previous_[installed_]) != 0) return false;
            ++installed_;
        }
        return true;
    }
    ~Signals() {
        while (installed_ != 0) {
            --installed_;
            sigaction(numbers_[installed_], &previous_[installed_], nullptr);
        }
    }

private:
    static constexpr int numbers_[] = {SIGINT, SIGTERM, SIGHUP, SIGPIPE};
    struct sigaction previous_[4]{};
    std::size_t installed_ = 0;
};

void Usage() {
    std::puts("Usage: zectrix-cli-host [options]\n"
              "Interactive maintenance CLI with synthetic Note4 hardware data.\n"
              "  --owner-delay-ms N    Delay owner safe points (0..60000, default 0)\n"
              "  --log-interval-ms N   Generate logs every N ms (0..60000, default 1000; 0 disables)\n"
              "  --log-burst N         Generate N startup log records (0..10000)\n"
              "  --help                Show this help\n"
              "Ctrl+C cancels a command, Ctrl+R reconnects, Ctrl+D exits.\n"
              "Piped commands execute in order; EOF completes replies and cancels streams.");
}

bool Number(const char* text, uint32_t maximum, uint32_t* value) {
    const auto end = text + std::strlen(text);
    const auto parsed = std::from_chars(text, end, *value);
    return parsed.ec == std::errc{} && parsed.ptr == end && *value <= maximum;
}

int Run(zectrix::cli::host::SimulationOptions options) {
    using namespace zectrix::cli;
    Signals signals;
    if (!signals.Open()) {
        std::perror("Cannot install host signal handlers");
        return 1;
    }
    host::StdioTransport transport;
    if (!transport.Open()) {
        std::fprintf(stderr, "Cannot open host terminal: %s\n", std::strerror(transport.error()));
        return 1;
    }
    LogBuffer logs;
    host::SimulatedPlatform platform(logs, options);
    PlatformControlDispatcher dispatcher(platform);
    DiagnosticExecutor executor(dispatcher, logs);
    CliSession session(transport, executor);
    platform.Start(dispatcher);
    constexpr char banner[] =
        "Host simulation: hardware snapshots are synthetic.\r\n"
        "Ctrl+C cancel | Ctrl+R reconnect | Ctrl+D exit\r\n";
    transport.Write(banner, sizeof(banner) - 1);
    while (stopped_signal.load(std::memory_order_relaxed) == 0) {
        transport.Pump(20);
        if (!transport.IsConnected() || transport.quit_requested()) break;
        transport.SetCommandActive(session.command_active());
        if (transport.input_closing() && executor.streaming()) {
            transport.Inject(0x03);
        } else if (transport.eof() && transport.input_empty() &&
                   !session.command_active() && session.line_size() != 0) {
            // Submit a final unterminated line through the production parser.
            transport.Inject('\n');
        }
        session.Poll();
        if (transport.reconnect_requested()) {
            session.Reset();
            transport.Reconnect();
            session.Poll();
        }
        if (transport.quit_requested()) break;
        if (transport.eof() && transport.input_empty() &&
            !session.command_active() && session.line_size() == 0) break;
    }
    session.Reset();
    dispatcher.Shutdown();
    platform.Stop();
    transport.Write("\r\n", 2);
    transport.DrainOutput(500);
    if (transport.error() != 0) {
        std::fprintf(stderr, "Host transport error: %s\n", std::strerror(transport.error()));
        return 1;
    }
    const int signal = stopped_signal.load(std::memory_order_relaxed);
    return signal == 0 ? 0 : 128 + signal;
}

}  // namespace

int main(int argc, char** argv) {
    zectrix::cli::host::SimulationOptions options;
    for (int index = 1; index < argc; index += 2) {
        if (std::strcmp(argv[index], "--help") == 0) {
            Usage();
            return 0;
        }
        uint32_t* value = nullptr;
        uint32_t maximum = 60000;
        if (std::strcmp(argv[index], "--owner-delay-ms") == 0) value = &options.owner_delay_ms;
        else if (std::strcmp(argv[index], "--log-interval-ms") == 0) value = &options.log_interval_ms;
        else if (std::strcmp(argv[index], "--log-burst") == 0) {
            value = &options.log_burst;
            maximum = 10000;
        }
        if (value == nullptr || index + 1 == argc || !Number(argv[index + 1], maximum, value)) {
            std::fprintf(stderr, "Invalid option or value: %s\nUse --help for usage.\n", argv[index]);
            return 2;
        }
    }
    try {
        return Run(options);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Host simulator failed: %s\n", error.what());
        return 1;
    }
}
