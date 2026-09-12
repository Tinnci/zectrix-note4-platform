#include "zectrix_book_web.h"
#include "zectrix_book_storage.h"
#include "zectrix_book_transfer_controller.h"
#include "sdkconfig.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <vector>

namespace {
using namespace zectrix;
using namespace connectivity;
namespace fs = std::filesystem;
constexpr char kCode[] = "ABCDEFGH2345";

std::string Contents(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
}

class Request final : public BookHttpRequest {
public:
    std::string path, body, output, authorization = std::string("Bearer ") + kCode;
    std::string type, attachment;
    int status = 0;
    uint32_t now = 100;
    std::size_t offset = 0, max_read = 0, fragment = 137, fail_after = SIZE_MAX;
    bool finished = false;
    std::function<void()> after_read;
    Request(BookHttpMethod verb, std::string url, std::string input = {})
        : path(std::move(url)), body(std::move(input)) {
        method = verb; uri = path.c_str(); content_length = body.size();
    }
    bool Header(const char* name, char* value, std::size_t capacity) override {
        assert(std::strcmp(name, "Authorization") == 0);
        if (authorization.empty() || authorization.size() >= capacity) return false;
        std::strcpy(value, authorization.c_str()); return true;
    }
    int Read(uint8_t* value, std::size_t capacity) override {
        max_read = std::max(max_read, capacity);
        if (offset >= fail_after) return -1;
        const auto size = std::min({capacity, fragment, body.size() - offset});
        std::memcpy(value, body.data() + offset, size); offset += size;
        if (after_read) after_read();
        return size;
    }
    bool Respond(int code, const char* mime, const char* filename = nullptr) override {
        assert(!status); status = code; type = mime; attachment = filename ? filename : ""; return true;
    }
    bool Write(const char* value, std::size_t size) override { output.append(value, size); return true; }
    bool Finish() override { finished = true; return true; }
    uint32_t NowMs() const override { return now; }
};

void Check(BookWebApi& api, Request& request, int status) {
    assert(api.Handle(request) && request.finished && request.status == status);
    assert(!api.Progress().active);
}

void StorageAndHttp(const fs::path& root) {
    fs::create_directory(root);
    std::ofstream(root / "original.txt") << "keep this book";
    storage::BookStorage books(root.c_str());
    storage::BookFile reader;
    assert(books.Open("original.txt", &reader) == ESP_OK);
    assert(books.BeginManagement() == ESP_ERR_INVALID_STATE);
    reader.Close();
    std::ofstream(root / ".upload.part") << "interrupted before reboot";
    assert(books.BeginManagement() == ESP_OK && !fs::exists(root / ".upload.part"));
    assert(books.Open("original.txt", &reader) == ESP_ERR_INVALID_STATE);
    BookWebApi api;
    api.Start(books, kCode, 0);
    Request page(BookHttpMethod::Get, "/");
    page.authorization.clear();
    Check(api, page, 200);
    assert(page.output.find("Your pocket library.") != std::string::npos && page.output.find(kCode) == std::string::npos);
    for (const auto& auth : {std::string(), std::string("Bearer incorrect12"), std::string("Bearer ") + kCode + "extra"}) {
        Request denied(BookHttpMethod::Put, "/api/books/stolen.txt", "bad"); denied.authorization = auth;
        Check(api, denied, 401);
        assert(!denied.offset && api.Progress().activity_ms == 0 && !fs::exists(root / "stolen.txt"));
    }
    for (const auto* name : {"../bad.txt", "%2e%2e%2fbad.txt", "bad%00.txt", "bad%5C.txt", "bad%GG.txt", "bad%0A.txt",
                             "%C0%AF.txt", "%ED%A0%80.txt", "%F4%90%80%80.txt", "bad.pdf", "bad%"}) {
        Request invalid(BookHttpMethod::Put, std::string("/api/books/") + name, "data");
        Check(api, invalid, 400);
        assert(!invalid.offset);
    }
    Request large(BookHttpMethod::Put, "/api/books/large.txt"); large.content_length = 0x400001;
    Check(api, large, 413);
    const std::string text = std::string(200000, 'x') + "\n中文，完整写入。";
    Request upload(BookHttpMethod::Put, "/api/books/%E4%B8%AD%E6%96%87.txt", text);
    Check(api, upload, 200);
    assert(upload.max_read <= 1024 && Contents(root / "中文.txt") == text);
    assert(api.Progress().uploaded == 1 && api.Progress().received == text.size());
    assert(!fs::exists(root / ".upload.part"));
    Request duplicate(BookHttpMethod::Put, "/api/books/original.txt", "replacement");
    Check(api, duplicate, 409);
    assert(!duplicate.offset && Contents(root / "original.txt") == "keep this book");
    Request download(BookHttpMethod::Get, "/api/books/%E4%B8%AD%E6%96%87.txt");
    Check(api, download, 200);
    assert(download.output == text && download.attachment == "中文.txt");
    Request empty(BookHttpMethod::Put, "/api/books/empty.txt");
    Check(api, empty, 200);
    assert(fs::exists(root / "empty.txt") && fs::file_size(root / "empty.txt") == 0);

    for (unsigned mode = 0; mode < 4; ++mode) {
        Request interrupted(BookHttpMethod::Put, "/api/books/partial.epub", std::string(5000, 'a'));
        if (mode == 0) interrupted.fail_after = 400;
        if (mode == 1) interrupted.after_read = [&] { api.Cancel(); };
        if (mode == 2) interrupted.after_read = [&] { interrupted.now += 120001; };
        if (mode == 3) interrupted.after_read = [&] { fs::create_directory(root / "partial.epub"); };
        Check(api, interrupted, mode == 3 ? 409 : 408);
        assert(!fs::is_regular_file(root / "partial.epub") && !fs::exists(root / ".upload.part"));
        fs::remove(root / "partial.epub");
        api.Start(books, kCode, 100);
    }
    Request retry(BookHttpMethod::Put, "/api/books/partial.epub", "complete after retry");
    Check(api, retry, 200);
    assert(Contents(root / "partial.epub") == "complete after retry");
    fs::create_symlink(root / "original.txt", root / "link.txt");
    Request link(BookHttpMethod::Delete, "/api/books/link.txt");
    Check(api, link, 400);
    assert(Contents(root / "original.txt") == "keep this book");
    Request remove(BookHttpMethod::Delete, "/api/books/partial.epub");
    Check(api, remove, 200);
    assert(!fs::exists(root / "partial.epub"));
    Request missing(BookHttpMethod::Delete, "/api/books/partial.epub");
    Check(api, missing, 404);

    for (unsigned i = 0; i < 65; ++i) {
        char name[32]; std::snprintf(name, sizeof(name), "A%03u.txt", i); std::ofstream(root / name) << "book";
    }
    std::ofstream(root / "quote\"<tag>.txt") << "safe filename";
    Request first(BookHttpMethod::Get, "/api/books");
    Check(api, first, 200);
    assert(first.output.find("A031.txt") != std::string::npos && first.output.find("A032.txt") == std::string::npos);
    assert(first.output.find("\"more\":true") != std::string::npos);
    Request next(BookHttpMethod::Get, "/api/books?after=A031.txt");
    Check(api, next, 200);
    assert(next.output.find("A031.txt") == std::string::npos && next.output.find("A063.txt") != std::string::npos);
    Request last(BookHttpMethod::Get, "/api/books?after=A063.txt");
    Check(api, last, 200);
    assert(last.output.find("\"more\":false") != std::string::npos && last.output.find("quote\\\"<tag>.txt") != std::string::npos);
    storage::BookSpace space;
    assert(books.Space(&space) == ESP_OK);
    std::ofstream(root / "filler.txt") << std::string(space.available, 'f');
    Request full(BookHttpMethod::Put, "/api/books/no-room.txt", "abc");
    Check(api, full, 507);
    assert(!fs::exists(root / "no-room.txt") && !fs::exists(root / ".upload.part"));
    Request finish(BookHttpMethod::Post, "/api/finish");
    Check(api, finish, 200);
    assert(api.Progress().finish);
    Request ended(BookHttpMethod::Get, "/api/books");
    Check(api, ended, 503);
    assert(books.EndManagement() == ESP_OK);
    assert(books.Open("original.txt", &reader) == ESP_OK);
}

class Radio final : public BookTransferRadio {
public:
    std::vector<std::string>& calls;
    WifiDriverResult start = WifiDriverResult::kPending, poll = WifiDriverResult::kReady, stop = WifiDriverResult::kReady;
    explicit Radio(std::vector<std::string>& events) : calls(events) {}
    WifiDriverResult Start(BookTransferMode, const WifiCredentials&) override { calls.emplace_back("radio-start"); return start; }
    WifiDriverResult Poll(BookTransferMode, char* address, std::size_t) override { std::strcpy(address, "192.168.4.1"); return poll; }
    WifiDriverResult Stop() override { calls.emplace_back("radio-stop"); return stop; }
};
class Server final : public BookTransferServer {
public:
    std::vector<std::string>& calls;
    BookTransferProgress progress{};
    bool start = true, stop = true;
    explicit Server(std::vector<std::string>& events) : calls(events) {}
    bool Start(storage::BookStorage&, const char*, uint32_t now) override {
        calls.emplace_back("http-start"); progress = {}; progress.activity_ms = now; return start;
    }
    BookTransferProgress Progress() const override { return progress; }
    bool Stop() override { calls.emplace_back("http-stop"); return stop; }
};

void Lifecycle(const fs::path& root) {
    fs::create_directory(root);
    storage::BookStorage books(root.c_str());
    WifiCredentials credentials;
    for (unsigned scenario = 0; scenario < 12; ++scenario) {
        std::vector<std::string> calls;
        Radio radio(calls); Server server(calls); BookTransfer transfer(radio, server);
        const uint32_t start = scenario == 9 || scenario == 11 ? UINT32_MAX - 200 : 0;
        assert(books.BeginManagement() == ESP_OK);
        if (scenario == 0) radio.poll = WifiDriverResult::kPending;
        if (scenario == 1) server.start = false;
        assert(transfer.Begin(books, BookTransferMode::Hotspot, credentials, kCode, start));
        transfer.Poll(start);
        if (scenario == 0) {
            transfer.Poll(start + BookTransfer::kStartupMs);
            assert(transfer.Snapshot().error == BookTransferError::Timeout);
        } else if (scenario == 1) {
            assert(transfer.Snapshot().error == BookTransferError::Server);
        } else if (scenario == 2) {
            transfer.Poll(start + BookTransfer::kIdleMs);
        } else if (scenario == 3 || scenario == 9 || scenario >= 10) {
            server.progress.uploaded = 1;
            server.progress.activity_ms = server.progress.completed_ms = start + 500;
            if (scenario >= 10) {
                transfer.Poll(start + 499);
                assert(transfer.Busy() && transfer.Snapshot().seconds_left == 30);
            }
            transfer.Poll(start + BookTransfer::kAfterUploadMs + 499);
            assert(transfer.Busy());
            transfer.Poll(start + BookTransfer::kAfterUploadMs + 500);
        } else if (scenario == 4) {
            server.progress.finish = true;
            transfer.Poll(start + 10);
            transfer.Poll(start + 509); assert(transfer.Busy());
            transfer.Poll(start + 510);
        } else if (scenario == 5) {
            transfer.Poll(start + 10, false);
            assert(transfer.Snapshot().error == BookTransferError::Power);
        } else if (scenario == 6) {
            transfer.Poll(start + 10, true, false);
            assert(transfer.Snapshot().error == BookTransferError::Policy);
        } else if (scenario == 7) {
            server.progress.active = true;
            server.progress.activity_ms = start + BookTransfer::kSessionMs;
            transfer.Poll(start + BookTransfer::kSessionMs);
            assert(transfer.Snapshot().error == BookTransferError::Timeout);
        } else {
            radio.stop = WifiDriverResult::kUnavailable;
            assert(!transfer.Stop());
            assert(transfer.Busy() && transfer.Snapshot().state == BookTransferState::Stopping);
            assert(books.BeginManagement() == ESP_ERR_INVALID_STATE);
            radio.stop = WifiDriverResult::kReady;
            transfer.Poll(start + 30);
        }
        assert(!transfer.Busy() && !transfer.Snapshot().code[0]);
        assert(calls[calls.size() - 2] == "http-stop" && calls.back() == "radio-stop");
        assert(books.BeginManagement() == ESP_OK && books.EndManagement() == ESP_OK);
        assert(transfer.Stop());
    }
}

void AppHttp(const fs::path& root, const fs::path& package) {
    fs::create_directory(root);
    storage::BookStorage books(root.c_str());
    assert(books.BeginManagement() == ESP_OK);
    BookWebApi api;
    api.Start(books, kCode, 0);
    const auto data = Contents(package);
    assert(data.size() > 1024);
    Request listing(BookHttpMethod::Get, "/api/books");
    Check(api, listing, 200);
#if CONFIG_ZECTRIX_ENABLE_RUNTIME
    assert(listing.output.find("\"apps_supported\":true") != std::string::npos);
    for (auto method : {BookHttpMethod::Get, BookHttpMethod::Put, BookHttpMethod::Delete}) {
        Request denied(method, "/api/apps/Calculator.zapp", data);
        denied.authorization.clear();
        Check(api, denied, 401);
        assert(!denied.offset);
    }
    Request installed(BookHttpMethod::Put, "/api/apps/Calculator.zapp", data);
    Check(api, installed, 200);
    assert(installed.max_read <= 1024 && Contents(root / ".app-Calculator.zapp") == data);
    assert(api.Progress().uploaded == 1);
    Request duplicate(BookHttpMethod::Put, "/api/apps/Calculator.zapp", data);
    Check(api, duplicate, 409);
    Request exported(BookHttpMethod::Get, "/api/apps/Calculator.zapp");
    Check(api, exported, 200);
    assert(exported.output == data && exported.attachment == "Calculator.zapp");
    Request book_list(BookHttpMethod::Get, "/api/books"), app_list(BookHttpMethod::Get, "/api/apps");
    Check(api, book_list, 200); Check(api, app_list, 200);
    assert(book_list.output.find("Calculator") == std::string::npos);
    assert(app_list.output.find("Calculator.zapp") != std::string::npos);
    for (const auto* path : {"/api/books/Calculator.zapp", "/api/apps/book.txt", "/api/apps/.zapp",
                             "/api/apps/%2e%2e%2fescape.zapp", "/api/apps?after=book.txt"}) {
        Request invalid(BookHttpMethod::Get, path);
        Check(api, invalid, 400);
    }
    Request oversized(BookHttpMethod::Put, "/api/apps/large.zapp");
    oversized.content_length = 33057;
    Check(api, oversized, 413);
    for (int fault = 0; fault < 5; ++fault) {
        auto damaged = data;
        if (fault == 0) damaged[8] = 2;
        if (fault == 1) damaged[10] = 4;
        if (fault == 2) damaged.back() = '\xc2';
        Request interrupted(BookHttpMethod::Put, "/api/apps/bad.zapp", damaged);
        if (fault == 3) interrupted.fail_after = 200;
        if (fault == 4) interrupted.after_read = [&] { api.Cancel(); };
        Check(api, interrupted, fault < 3 ? 400 : 408);
        assert(!fs::exists(root / ".app-bad.zapp") && !fs::exists(root / ".upload.part"));
        api.Start(books, kCode, 0);
    }
    Request retry(BookHttpMethod::Put, "/api/apps/bad.zapp", data);
    Check(api, retry, 200);
    Request raw(BookHttpMethod::Put, "/api/apps/Plain.lua", "while true do end");
    Check(api, raw, 200);
    for (unsigned i = 0; i < 35; ++i) {
        char name[32]; std::snprintf(name, sizeof(name), ".app-A%03u.zapp", i);
        std::ofstream(root / name, std::ios::binary).write(data.data(), data.size());
    }
    Request first(BookHttpMethod::Get, "/api/apps"), next(BookHttpMethod::Get, "/api/apps?after=A031.zapp");
    Check(api, first, 200); Check(api, next, 200);
    assert(first.output.find("A031.zapp") != std::string::npos && first.output.find("A032.zapp") == std::string::npos);
    assert(next.output.find("A032.zapp") != std::string::npos && next.output.find("\"more\":false") != std::string::npos);
    Request remove(BookHttpMethod::Delete, "/api/apps/Calculator.zapp");
    Check(api, remove, 200);
    assert(!fs::exists(root / ".app-Calculator.zapp"));
#else
    assert(listing.output.find("\"apps_supported\":false") != std::string::npos);
    for (auto method : {BookHttpMethod::Get, BookHttpMethod::Put, BookHttpMethod::Delete}) {
        Request disabled(method, "/api/apps/Calculator.zapp", data);
        Check(api, disabled, 503);
        assert(!disabled.offset && !fs::exists(root / ".upload.part"));
    }
#endif
    assert(books.EndManagement() == ESP_OK);
}

void Scenes() {
    using namespace app;
    constexpr sdk::InputEvent ok{sdk::Button::Ok, sdk::InputAction::Click};
    constexpr sdk::InputEvent down{sdk::Button::Down, sdk::InputAction::Click};
    constexpr sdk::InputEvent back{sdk::Button::Ok, sdk::InputAction::LongPress};
    BookTransferController controller;
    assert(sdk::IsOk(controller.Start()));
    assert(controller.Handle(back) == BookTransferDecision::Back);
    assert(controller.Handle(down) == BookTransferDecision::RenderFast && controller.station_selected());
    assert(controller.Handle(ok) == BookTransferDecision::Station && controller.scene() == BookTransferScene::Session);
    BookTransferSnapshot status;
    status.state = BookTransferState::Sharing;
    assert(controller.Update(status, 0) == BookTransferDecision::RenderQuality);
    status.expected = 1000; status.received = 150;
    assert(controller.Update(status, 500000) == BookTransferDecision::None);
    assert(controller.Update(status, 1000000) == BookTransferDecision::RenderFast);
    controller.Presented(false);
    assert(controller.Update(status, 1000001) == BookTransferDecision::RenderQuality);
    status.uploaded = 1;
    assert(controller.Update(status, 1500000) == BookTransferDecision::None);
    assert(controller.Update(status, 2000000) == BookTransferDecision::RenderFast);
    assert(controller.Handle(ok) == BookTransferDecision::Stop);
    status.state = BookTransferState::Complete;
    assert(controller.Update(status, 1500000) == BookTransferDecision::RenderQuality);
    assert(controller.Handle(ok) == BookTransferDecision::Reader);
    assert(controller.Handle(back) == BookTransferDecision::Stop && controller.scene() == BookTransferScene::Mode);
    assert(controller.Handle(ok) == BookTransferDecision::Station);
    assert(controller.Handle({sdk::Button::Down, sdk::InputAction::LongPress}) == BookTransferDecision::Shutdown);
    controller.Stop();
    controller.Stop();
    assert(controller.snapshot().state == BookTransferState::Off);
    assert(controller.Handle(ok) == BookTransferDecision::None);
    controller.Presented(false);
    assert(controller.Update(status, 3000000) == BookTransferDecision::None);
    assert(sdk::IsOk(controller.Start()));
    assert(controller.scene() == BookTransferScene::Mode && controller.station_selected());
    assert(controller.Handle(ok) == BookTransferDecision::Station);
    status.state = BookTransferState::Failed;
    controller.Update(status, 3000001);
    assert(controller.Handle(ok) == BookTransferDecision::RenderQuality);
    assert(controller.scene() == BookTransferScene::Mode);
    assert(controller.Handle(back) == BookTransferDecision::Back);
}
}  // namespace

int main(int argc, char** argv) {
    assert(argc == 3);
    fs::create_directories(argv[1]);
    StorageAndHttp(fs::path(argv[1]) / "http");
    Lifecycle(fs::path(argv[1]) / "lifecycle");
    Scenes();
    AppHttp(fs::path(argv[1]) / "apps", argv[2]);
    std::puts("PASS: streamed web books, interrupted uploads, storage isolation, radio lifecycle and transfer scenes.");
}
