#include "zectrix_host_protocol.h"
#include "zectrix_host_books.h"
#include "zectrix_cli_diagnostics.h"
#include "zectrix_usb_manager.h"
#include "zectrix_storage_service.h"
#include "zectrix_language_setting.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace {
using namespace zectrix;
using namespace zectrix::host;
uint64_t now_ms = 0;
uint64_t Clock() { return now_ms; }
std::map<std::string, uint32_t> preferences;
bool save_fails = false;
unsigned saves = 0;

class Transport final : public cli::CliTransport {
public:
    bool IsConnected() const override { return connected; }
    std::size_t Read(uint8_t* data, std::size_t capacity) override {
        const auto count = std::min({capacity, input.size(), read_limit});
        for (std::size_t i = 0; i < count; ++i) { data[i] = input.front(); input.pop_front(); }
        read_bytes += count;
        return count;
    }
    bool Write(const char* data, std::size_t size) override {
        if (!writable) return false;
        output.append(data, size);
        return true;
    }
    void DiscardInput() override { input.clear(); }
    void Feed(const std::string& data) { for (const auto byte : data) input.push_back(static_cast<uint8_t>(byte)); }
    void Feed(const Frame& frame) {
        uint8_t wire[kWireSize];
        const auto size = EncodeFrame(frame, wire, sizeof(wire));
        assert(size);
        for (std::size_t i = 0; i < size; ++i) input.push_back(wire[i]);
    }
    bool connected = true, writable = true;
    std::size_t read_limit = 128, read_bytes = 0;
    std::deque<uint8_t> input;
    std::string output;
};

class Owner final : public cli::ControlOwner {
public:
    bool IsCurrentTaskOwner() const override { return true; }
    void Wake() override {}
    cli::ControlStatus Inspect(const cli::ControlRequest&, cli::ControlResult*) override {
        ++inspections;
        return cli::ControlStatus::kOk;
    }
    unsigned inspections = 0;
};
class Settings final : public host::Settings {
public:
    Status Get(Setting key, uint32_t* value) override { *value = values[static_cast<unsigned>(key)]; return Status::Ok; }
    Status Set(Setting key, uint32_t value) override { values[static_cast<unsigned>(key)] = value; return Status::Ok; }
    uint32_t values[3]{};
};

class Fixture {
public:
    explicit Fixture(const std::string& root)
        : protocol(channel, Clock), dispatcher(owner), executor(dispatcher, logs, &protocol),
          terminal(transport, executor), storage(root.c_str()), books(channel, settings), root_(root) {
        now_ms = 0;
        std::filesystem::create_directories(root);
        assert(storage.BeginManagement() == ESP_OK);
        books.Start(storage);
        terminal.Poll();
    }
    ~Fixture() { terminal.Reset(); books.Stop(); }
    void Start() {
        transport.Feed("host start 1\n");
        for (unsigned i = 0; i < 100 && !terminal.binary_active(); ++i) terminal.Poll();
        assert(terminal.binary_active());
        assert(transport.output.find("N4USB 1 ") != std::string::npos);
        session = channel.Session();
        next_id = 0;
        transport.output.clear();
    }
    void Step() { terminal.Poll(); books.Poll(); dispatcher.Dispatch(); }
    Frame Request(Operation operation, const std::string& data = "") {
        Frame request;
        request.operation = static_cast<uint8_t>(operation);
        request.id = ++next_id;
        request.session = session;
        assert(data.size() <= request.payload.size());
        request.size = data.size();
        std::memcpy(request.payload.data(), data.data(), data.size());
        return request;
    }
    Frame Receive() {
        for (unsigned i = 0; i < 100 && transport.output.find('\0') == std::string::npos; ++i) Step();
        const auto end = transport.output.find('\0');
        assert(end != std::string::npos);
        Frame response;
        assert(DecodeFrame(reinterpret_cast<const uint8_t*>(transport.output.data()), end, &response));
        return response;
    }
    Frame Call(Operation operation, const std::string& data = "") {
        transport.output.clear();
        transport.Feed(Request(operation, data));
        auto response = Receive();
        assert(response.operation == (static_cast<uint8_t>(operation) | 0x80));
        assert(response.session == session && response.id == next_id);
        return response;
    }
    void Close(const std::string& tail = "") {
        transport.output.clear();
        transport.Feed(std::string(1, '\0'));
        transport.Feed(Request(Operation::Close));
        transport.Feed(tail);
        assert(Receive().status == Status::Ok);
        for (unsigned i = 0; i < 5; ++i) Step();
        assert(!terminal.binary_active() && !channel.Session());
        assert(transport.output.find("zectrix> ") != std::string::npos);
    }
    bool Exists(const char* name) const { return std::filesystem::exists(root_ + "/" + name); }
    std::string Contents(const char* name) const {
        std::ifstream file(root_ + "/" + name, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(file), {});
    }
    Transport transport;
    Channel channel;
    Protocol protocol;
    Owner owner;
    cli::PlatformControlDispatcher dispatcher;
    cli::LogBuffer logs;
    cli::DiagnosticExecutor executor;
    cli::CliSession terminal;
    storage::BookStorage storage;
    Settings settings;
    BookSession books;
    uint32_t session = 0, next_id = 0;
    std::string root_;
};

std::string Number(uint32_t value) {
    std::string result(4, '\0');
    Write32(reinterpret_cast<uint8_t*>(result.data()), value);
    return result;
}

void TestFrames() {
    Frame frame, decoded;
    frame.operation = 6;
    frame.id = 0x12345678;
    frame.session = 0xabcdef12;
    uint8_t encoded[kWireSize];
    for (unsigned mode = 0; mode < 3; ++mode) {
        for (std::size_t size = 0; size <= kPayloadSize; ++size) {
            frame.size = size;
            for (std::size_t i = 0; i < size; ++i) frame.payload[i] = mode == 0 ? i % 256 : mode == 1 ? 0 : 0xff;
            const auto written = EncodeFrame(frame, encoded, sizeof(encoded));
            assert(written <= sizeof(encoded) && encoded[written - 1] == 0);
            assert(DecodeFrame(encoded, written - 1, &decoded));
            assert(decoded.operation == frame.operation && decoded.id == frame.id && decoded.session == frame.session);
            assert(decoded.size == size && std::equal(frame.payload.begin(), frame.payload.begin() + size, decoded.payload.begin()));
        }
    }
    frame.size = kPayloadSize + 1;
    assert(!EncodeFrame(frame, encoded, sizeof(encoded)));
    for (const auto& invalid : std::vector<std::vector<uint8_t>>{{0}, {2, 0}, {255, 1}, {1, 1}})
        assert(!DecodeFrame(invalid.data(), invalid.size(), &decoded));
}

void TestChannelCancellation() {
    Channel channel;
    assert(!channel.Connect());
    channel.Enable();
    const auto first = channel.Connect();
    Frame request, result;
    request.id = 1;
    request.session = first;
    assert(channel.Submit(request) == Status::Ok);
    assert(channel.Submit(request) == Status::Busy);
    channel.Disconnect(first);
    assert(!channel.TakeRequest(&result));
    const auto second = channel.Connect();
    assert(second != first && channel.Submit(request) == Status::Cancelled);
    request.session = second;
    assert(channel.Submit(request) == Status::Ok && channel.TakeRequest(&result));
    std::thread disconnect([&] { channel.Disconnect(second); });
    disconnect.join();
    assert(!channel.Connect());
    channel.Complete(result);
    assert(!channel.TakeReply(second, &result));
    const auto third = channel.Connect();
    assert(third && third != second);
    channel.Disconnect(first);
    assert(channel.Session() == third);
    request.session = third;
    assert(channel.Submit(request) == Status::Ok);
    channel.Complete(result);
    assert(channel.TakeRequest(&result));
    channel.Disable();
    channel.Complete(result);
    assert(!channel.Connect() && !channel.TakeReply(third, &result));
}

void TestBooks(const std::string& root) {
    Fixture f(root);
    f.transport.read_limit = 7;
    f.Start();
    storage::BookFile reader;
    assert(f.storage.Open("book.epub", &reader) == ESP_ERR_INVALID_STATE);
    assert(f.storage.BeginManagement() == ESP_ERR_INVALID_STATE);
    std::string data;
    for (unsigned i = 0; i < 9000; ++i) data.push_back(i % 256);
    data += "\n\x03\x04\x12uptime\n";
    assert(f.Call(Operation::UploadBegin, Number(data.size()) + "中文.epub").status == Status::Ok);
    assert(!f.Exists("中文.epub") && f.Exists(".upload.part"));
    for (std::size_t offset = 0; offset < data.size(); offset += kChunkSize) {
        const auto part = data.substr(offset, kChunkSize);
        const auto reply = f.Call(Operation::UploadChunk, Number(offset) + part);
        assert(reply.status == Status::Ok && Read32(reply.payload.data()) == offset + part.size());
        assert(f.terminal.binary_active() && !f.owner.inspections);
    }
    assert(f.Call(Operation::UploadCommit).status == Status::Ok);
    assert(!f.Exists(".upload.part") && f.Contents("中文.epub") == data);
    assert(f.books.snapshot().uploaded == 1);
    assert(f.Call(Operation::UploadBegin, Number(3) + "中文.epub").status == Status::Exists);
    assert(f.Contents("中文.epub") == data);
    assert(f.Call(Operation::UploadBegin, Number(3) + "../bad.txt").status == Status::Invalid);
    assert(f.Call(Operation::UploadBegin, Number(UINT32_MAX) + "huge.txt").status == Status::NoSpace);
    assert(f.Call(Operation::UploadBegin, Number(0) + "empty.txt").status == Status::Ok);
    assert(f.Call(Operation::UploadCommit).status == Status::Ok);
    assert(f.Call(Operation::ReadOpen, "empty.txt").status == Status::Ok);
    assert(f.books.snapshot().state == TransferState::Complete);

    auto response = f.Call(Operation::ReadOpen, "中文.epub");
    assert(response.status == Status::Ok && Read32(response.payload.data()) == data.size());
    assert(f.Call(Operation::UploadBegin, Number(3) + "busy.txt").status == Status::Busy);
    std::string received;
    while (received.size() < data.size()) {
        response = f.Call(Operation::Read, Number(received.size()));
        assert(response.status == Status::Ok);
        received.append(reinterpret_cast<const char*>(response.payload.data()), response.size);
    }
    assert(received == data);
    for (unsigned i = 0; i < 11; ++i) {
        const auto name = "list-" + std::to_string(i) + ".txt";
        assert(f.Call(Operation::UploadBegin, Number(0) + name).status == Status::Ok);
        assert(f.Call(Operation::UploadCommit).status == Status::Ok);
    }
    std::string after;
    unsigned listed = 0;
    do {
        response = f.Call(Operation::List, after);
        assert(response.status == Status::Ok && response.payload[0] <= 8);
        std::size_t cursor = 2;
        for (unsigned i = 0; i < response.payload[0]; ++i) {
            const auto size = response.payload[cursor];
            std::string name(reinterpret_cast<const char*>(response.payload.data() + cursor + 5), size);
            assert(after.empty() || std::strcmp(name.c_str(), after.c_str()) > 0);
            after = name;
            cursor += 5 + size;
            ++listed;
        }
        assert(cursor == response.size);
    } while (response.payload[1]);
    assert(listed == 13);
    assert(f.Call(Operation::Info).size == 16);
    f.Close("uptime\n");
    assert(!f.owner.inspections);
    f.books.Stop();
    assert(f.storage.Open("中文.epub", &reader) == ESP_OK);
    assert(f.storage.BeginManagement() == ESP_ERR_INVALID_STATE);
    reader.Close();
    assert(f.storage.BeginManagement() == ESP_OK);
    assert(f.storage.EndManagement() == ESP_OK);
}

void TestInterruptedBooks(const std::string& root) {
    Fixture f(root);
    f.Start();
    assert(f.Call(Operation::UploadBegin, Number(8) + "partial.txt").status == Status::Ok);
    assert(f.Call(Operation::UploadChunk, Number(0) + "part").status == Status::Ok);
    assert(f.Call(Operation::UploadCommit).status == Status::Invalid);
    assert(!f.Exists("partial.txt") && !f.Exists(".upload.part"));
    assert(f.Call(Operation::UploadBegin, Number(8) + "offset.txt").status == Status::Ok);
    assert(f.Call(Operation::UploadChunk, Number(2) + "part").status == Status::Invalid);
    assert(!f.Exists(".upload.part"));
    assert(f.Call(Operation::UploadBegin, Number(1) + "length.txt").status == Status::Ok);
    assert(f.Call(Operation::UploadChunk, Number(0) + "too long").status == Status::Invalid);
    assert(!f.Exists(".upload.part"));
    assert(f.Call(Operation::UploadBegin, Number(8) + "cancel.txt").status == Status::Ok);
    f.books.Cancel();
    assert(!f.Exists(".upload.part"));
    assert(f.Call(Operation::UploadChunk, Number(0) + "part").status == Status::Cancelled);
    f.Close();
    f.Start();
    assert(f.Call(Operation::UploadBegin, Number(8) + "disconnect.txt").status == Status::Ok);
    f.transport.connected = false;
    f.Step();
    assert(!f.Exists(".upload.part") && !f.terminal.binary_active());
    f.transport.connected = true;
    f.Start();
    assert(f.Call(Operation::UploadBegin, Number(8) + "shutdown.txt").status == Status::Ok);
    f.books.Stop();
    assert(!f.Exists(".upload.part") && !f.channel.Session());
    f.Close();
    assert(f.storage.BeginManagement() == ESP_OK);
    assert(f.storage.EndManagement() == ESP_OK);
}

void TestBinaryFailures(const std::string& root) {
    Fixture f(root);
    f.Start();
    f.transport.Feed(std::string(4000, 'x') + std::string(1, '\0'));
    assert(f.Receive().status == Status::Invalid);
    assert(f.terminal.binary_active() && !f.channel.Session());
    f.transport.Feed("\x03uptime\n");
    for (unsigned i = 0; i < 10; ++i) f.Step();
    assert(!f.owner.inspections && f.terminal.binary_active());
    f.Close();
    f.Start();
    Frame stale = f.Request(Operation::Info);
    ++stale.session;
    f.transport.Feed(stale);
    assert(f.Receive().status == Status::Invalid);
    f.Close();
    f.Start();
    const auto info = f.Request(Operation::Info);
    f.transport.Feed(info);
    assert(f.Receive().status == Status::Ok);
    f.transport.output.clear();
    f.transport.Feed(info);
    assert(f.Receive().status == Status::Invalid);
    f.Close();
    f.Start();
    assert(f.Call(Operation::UploadBegin, Number(8) + "timeout.txt").status == Status::Ok);
    f.transport.output.clear();
    f.transport.Feed(std::string(1, '\xff'));
    now_ms += kSessionTimeoutMs;
    assert(f.Receive().status == Status::Timeout);
    assert(!f.Exists(".upload.part") && f.terminal.binary_active());
    f.Close();
    f.Start();
    f.transport.Feed(std::string(10000, 'x'));
    const auto before = f.transport.read_bytes;
    f.Step();
    assert(f.transport.read_bytes - before <= 2048);
    f.transport.connected = false;
    f.Step();
    f.transport.connected = true;
    f.Start();
    f.transport.Feed(f.Request(Operation::Info));
    f.transport.writable = false;
    for (unsigned i = 0; i < 5; ++i) f.Step();
    assert(!f.channel.Session() && f.terminal.binary_active());
    f.transport.Feed("uptime\n");
    for (unsigned i = 0; i < 5; ++i) f.Step();
    assert(!f.owner.inspections);
    f.transport.connected = false;
    f.Step();
    f.transport.connected = f.transport.writable = true;
    f.Step();
    assert(!f.terminal.binary_active());
}

void TestSettingsAndControls() {
    storage::StorageService* storage = nullptr;
    assert(storage::StorageService::Create(&storage) == ESP_OK);
    bool language_saved = true, cover_saved = true;
    app::SleepCoverStyle cover = app::SleepCoverStyle::Dashboard;
    app::UsbSettings settings(*storage, language_saved, cover, cover_saved);
    uint32_t value = 0;
    assert(settings.Get(Setting::AutoShowcase, &value) == Status::Ok && value == 0);
    assert(settings.Set(Setting::AutoShowcase, 2) == Status::Invalid && !saves);
    assert(settings.Set(Setting::Language, 99) == Status::Invalid && !saves);
    save_fails = true;
    assert(settings.Set(Setting::Language, 1) == Status::NotSaved && !language_saved);
    assert(i18n::CurrentLanguage() == i18n::Language::Chinese);
    assert(settings.Set(Setting::SleepCover, 2) == Status::NotSaved && !cover_saved);
    assert(cover == app::SleepCoverStyle::Blank);
    assert(settings.Set(Setting::AutoShowcase, 1) == Status::IoError);
    assert(settings.Get(Setting::AutoShowcase, &value) == Status::Ok && value == 0);
    const auto attempts = saves;
    save_fails = false;
    assert(settings.Set(Setting::Language, 1) == Status::Ok && language_saved);
    assert(settings.Set(Setting::SleepCover, 2) == Status::Ok && cover_saved);
    assert(saves == attempts + 2);
    assert(settings.Set(Setting::AutoShowcase, 1) == Status::Ok);
    assert(settings.Get(Setting::AutoShowcase, &value) == Status::Ok && value == 1);
    delete storage;

    app::UsbManagerController controller;
    using D = app::UsbDecision;
    using B = sdk::Button;
    using A = sdk::InputAction;
    controller.Start(false);
    assert(controller.Handle({B::Ok, A::Click}) == D::Retry);
    controller.Start(true);
    assert(controller.Handle({B::Ok, A::Click}) == D::Cancel);
    assert(controller.Handle({B::Ok, A::LongPress}) == D::Back);
    assert(controller.Handle({B::Down, A::LongPress}) == D::Shutdown);
    assert(controller.Handle({B::Up, A::LongPress}) == D::None);
    Snapshot snapshot;
    snapshot.state = TransferState::Uploading;
    snapshot.expected = 1000;
    assert(controller.Update(snapshot, 0) == D::RenderQuality);
    unsigned renders = 0;
    for (unsigned i = 1; i <= 1000; ++i) {
        snapshot.transferred = i;
        const auto result = controller.Update(snapshot, i * 1000);
        if (result != D::None) ++renders;
    }
    assert(renders <= 1);
    for (unsigned i = 1; i < 100; ++i) {
        snapshot.state = i % 2 ? TransferState::Complete : TransferState::Uploading;
        assert(controller.Update(snapshot, 1000000 + i * 1000) == D::None);
    }
    controller.Presented(false);
    assert(controller.Update(snapshot, 1000001) == D::RenderQuality);
    ++snapshot.settings_revision;
    assert(controller.Update(snapshot, 1000002) == D::RenderQuality);
    snapshot.state = TransferState::Cancelled;
    assert(controller.Update(snapshot, 1000003) == D::RenderQuality);
}
}

namespace zectrix::storage {
struct StorageService::Impl {};
esp_err_t StorageService::Create(StorageService** output) { *output = new StorageService(new Impl); return ESP_OK; }
StorageService::~StorageService() { delete impl_; }
esp_err_t StorageService::GetUInt32(const char* key, uint32_t* value) const {
    const auto found = preferences.find(key);
    if (found == preferences.end()) return ESP_ERR_NOT_FOUND;
    *value = found->second;
    return ESP_OK;
}
esp_err_t StorageService::SetUInt32(const char* key, uint32_t value) {
    ++saves;
    if (save_fails) return ESP_FAIL;
    preferences[key] = value;
    return ESP_OK;
}
}

int main(int argc, char** argv) {
    assert(argc == 2);
    const std::string root = argv[1];
    TestFrames();
    TestChannelCancellation();
    TestBooks(root + "/books");
    TestInterruptedBooks(root + "/interrupted");
    TestBinaryFailures(root + "/binary");
    TestSettingsAndControls();
    std::printf("USB object sizes: channel=%zu protocol=%zu book-session=%zu controller=%zu; payload=%zu wire=%zu\n",
        sizeof(Channel), sizeof(Protocol), sizeof(BookSession), sizeof(app::UsbManagerController), kPayloadSize, kWireSize);
}
