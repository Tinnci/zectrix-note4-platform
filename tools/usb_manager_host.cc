#include "cli_host/stdio_transport.h"
#include "note4_cli_diagnostics.h"
#include "note4_host_protocol.h"
#include "note4_host_books.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <csignal>
#include <cstdio>
#include <thread>

namespace {
std::atomic<bool> stopped{false};
void Stop(int) { stopped.store(true); }

class Settings final : public note4::host::Settings {
public:
    note4::host::Status Get(note4::host::Setting key, uint32_t* value) override {
        *value = values[static_cast<unsigned>(key)];
        return note4::host::Status::Ok;
    }
    note4::host::Status Set(note4::host::Setting key, uint32_t value) override {
        const auto index = static_cast<unsigned>(key);
        if (value > (index == 2 ? 2u : 1u)) return note4::host::Status::Invalid;
        values[index] = value;
        return note4::host::Status::Ok;
    }
    uint32_t values[3]{};
};

class Diagnostics final : public note4::cli::ControlOwner {
public:
    bool IsCurrentTaskOwner() const override { return true; }
    void Wake() override {}
    note4::cli::ControlStatus Inspect(const note4::cli::ControlRequest&, note4::cli::ControlResult*) override {
        return note4::cli::ControlStatus::kUnavailable;
    }
};
}

int main(int argc, char** argv) {
    if (argc != 2) { std::fprintf(stderr, "Usage: usb-manager-host BOOK_DIRECTORY\n"); return 2; }
    using namespace note4;
    std::signal(SIGTERM, Stop);
    std::signal(SIGINT, Stop);
    std::signal(SIGHUP, Stop);
    std::signal(SIGPIPE, SIG_IGN);
    cli::host::StdioTransport transport;
    if (!transport.Open()) {
        std::fprintf(stderr, "PTY transport initialization failed: %s\n", std::strerror(transport.error()));
        return 1;
    }
    host::Channel channel;
    host::Protocol protocol(channel);
    Diagnostics diagnostics;
    cli::PlatformControlDispatcher dispatcher(diagnostics);
    cli::LogBuffer logs;
    cli::DiagnosticExecutor executor(dispatcher, logs, &protocol);
    cli::CliSession terminal(transport, executor);
    std::atomic<bool> ready{false}, storage_failed{false};
    std::thread owner([&] {
        storage::BookStorage storage(argv[1]);
        Settings settings;
        host::BookSession session(channel, settings);
        const auto mounted = storage.BeginManagement();
        if (mounted != ESP_OK) {
            std::fprintf(stderr, "Book mount failed: root=%s error=%d\n", argv[1], mounted);
            storage_failed = true;
            ready = true;
            return;
        }
        session.Start(storage);
        ready = true;
        while (!stopped.load()) {
            session.Poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        session.Stop();
    });
    while (!ready.load()) std::this_thread::yield();
    if (!storage_failed.load()) {
        while (!stopped.load()) {
            transport.Pump(1);
            if (!transport.IsConnected() || transport.quit_requested()) break;
            transport.SetBinaryActive(terminal.binary_active());
            terminal.Poll();
            if (transport.reconnect_requested()) { terminal.Reset(); transport.Reconnect(); }
            if (transport.eof()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    terminal.Reset();
    dispatcher.Shutdown();
    stopped = true;
    owner.join();
    transport.DrainOutput(100);
    return storage_failed.load() ? 1 : 0;
}
