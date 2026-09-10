#include "cli_host/stdio_transport.h"
#include "zectrix_cli_diagnostics.h"
#include "zectrix_host_protocol.h"
#include "zectrix_host_books.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <thread>

namespace {
std::atomic<bool> stopped{false};
void Stop(int) { stopped.store(true); }

class Settings final : public zectrix::host::Settings {
public:
    zectrix::host::Status Get(zectrix::host::Setting key, uint32_t* value) override {
        *value = values[static_cast<unsigned>(key)];
        return zectrix::host::Status::Ok;
    }
    zectrix::host::Status Set(zectrix::host::Setting key, uint32_t value) override {
        const auto index = static_cast<unsigned>(key);
        if (value > (index == 2 ? 2u : 1u)) return zectrix::host::Status::Invalid;
        values[index] = value;
        return zectrix::host::Status::Ok;
    }
    uint32_t values[3]{};
};

class Diagnostics final : public zectrix::cli::ControlOwner {
public:
    bool IsCurrentTaskOwner() const override { return true; }
    void Wake() override {}
    zectrix::cli::ControlStatus Inspect(const zectrix::cli::ControlRequest&, zectrix::cli::ControlResult*) override {
        return zectrix::cli::ControlStatus::kUnavailable;
    }
};
}

int main(int argc, char** argv) {
    if (argc != 2) { std::fprintf(stderr, "Usage: usb-manager-host BOOK_DIRECTORY\n"); return 2; }
    using namespace zectrix;
    std::signal(SIGTERM, Stop);
    std::signal(SIGINT, Stop);
    std::signal(SIGHUP, Stop);
    std::signal(SIGPIPE, SIG_IGN);
    cli::host::StdioTransport transport;
    if (!transport.Open()) return 1;
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
        if (storage.BeginManagement() != ESP_OK) { storage_failed = true; ready = true; return; }
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
