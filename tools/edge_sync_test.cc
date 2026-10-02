#include "zectrix_edge_sync.h"
#include "zectrix_bthome.h"
#include "zectrix_book_storage.h"
#include <array>
#include <cassert>
#include <cstring>
#include <filesystem>
using namespace zectrix;
namespace {
void Write32(uint8_t* p, uint32_t n) { for (unsigned i = 0; i < 4; ++i) p[i] = n >> (i * 8); }
std::array<uint8_t, storage::edge::kFileSize> Page(uint32_t revision) {
    std::array<uint8_t, storage::edge::kFileSize> page{};
    std::memcpy(page.data(), "ZEP1", 4); page[4] = 1;
    Write32(page.data() + 8, revision); Write32(page.data() + 12, 1704067200);
    Write32(page.data() + 16, 1704153600); Write32(page.data() + 20, 1704070800);
    page.back() = 0x81;
    return page;
}
class Credentials final : public connectivity::WifiCredentialSource {
public:
    bool missing = false;
    connectivity::WifiCredentialResult Load(connectivity::WifiCredentials* credentials) override {
        if (missing) return connectivity::WifiCredentialResult::kUnavailable;
        std::strcpy(credentials->ssid.data(), "test"); return connectivity::WifiCredentialResult::kAvailable;
    }
};
class Driver final : public connectivity::WifiBackendDriver {
public:
    using R = connectivity::WifiDriverResult;
    int stops = 0, starts = 0;
    bool waiting = false, cleanup_failure = false, invalid = false;
    R StartStation(const connectivity::WifiCredentials&) override { ++starts; return R::kPending; }
    R PollAssociation() override { return waiting ? R::kPending : R::kReady; }
    R PollIp() override { return R::kReady; }
    R Resolve(companion::ResourceCapability) override { return R::kReady; }
    R OpenTls(companion::ResourceCapability) override { return R::kReady; }
    R Fetch(companion::ResourceCapability, uint8_t* buffer, std::size_t capacity, std::size_t* size) override {
        auto page = Page(7); if (invalid) page[0] = 'X';
        assert(capacity == page.size()); std::memcpy(buffer, page.data(), page.size()); *size = page.size();
        return R::kReady;
    }
    R StopStation() override { ++stops; return cleanup_failure ? R::kUnavailable : R::kReady; }
};
void TransferTests() {
    const std::array<uint8_t, 12> expected{0xd2, 0xfc, 0x44, 0x00, 7, 0x01, 80, 0x0c, 0x74, 0x0e, 0x16, 1};
    assert(connectivity::BTHomePower(7, 80, 3700, true) == expected);
    assert(connectivity::BTHomePower(255, 101, 0, false)[6] == 100);
    using Result = connectivity::EdgeSync::Result;
    std::array<uint8_t, storage::edge::kFileSize> buffer{};
    for (unsigned mode = 0; mode < 5; ++mode) {
        Credentials credentials; Driver driver;
        credentials.missing = mode == 1; driver.waiting = mode == 2 || mode == 3;
        driver.cleanup_failure = mode == 3; driver.invalid = mode == 4;
        connectivity::EdgeSync transfer(credentials, driver);
        const uint32_t start = UINT32_MAX - 100;
        assert(!transfer.Begin(buffer.data(), buffer.size() - 1, 1000, start));
        assert(transfer.Begin(buffer.data(), buffer.size(), 1000, start));
        Result result = Result::Pending;
        for (uint32_t i = 0; result == Result::Pending && i < 500; ++i) result = transfer.Poll(start + i * 10);
        assert(result == (mode == 0 ? Result::Success : mode == 3 ? Result::StopFailed : Result::Failed));
        if (mode != 1) assert(driver.starts == 1 && driver.stops > 0);
        else assert(driver.starts == 0 && driver.stops == 0);
    }
    Credentials credentials; Driver driver;
    connectivity::EdgeSync transfer(credentials, driver);
    assert(transfer.Begin(buffer.data(), buffer.size(), 1000, 0));
    transfer.Poll(0); transfer.Poll(10); transfer.Cancel(20);
    assert(transfer.Poll(30) == Result::Cancelled && driver.stops == 1);
}
void CacheTests(const char* root) {
    std::filesystem::create_directories(root);
    storage::BookStorage books(root);
    auto page = Page(1);
    assert(books.BeginManagement() == ESP_OK);
    storage::BookUpload upload;
    assert(books.BeginEdgePageUpload(&upload) == storage::BookWriteResult::Ok);
    assert(upload.Write(page.data(), page.size()) == storage::BookWriteResult::Ok);
    assert(upload.Commit() == storage::BookWriteResult::Ok);
    assert(books.EndManagement() == ESP_OK);
    storage::BookFile file; storage::edge::Page info;
    std::array<uint8_t, storage::edge::kHeaderSize> header{};
    assert(books.OpenEdgePage(&file) == ESP_OK && file.Read(0, header.data(), header.size()));
    assert(storage::edge::Decode(header.data(), file.Size(), &info) && info.revision == 1);
    assert(info.Fresh(1704067200) && !info.Fresh(1704153600));
    file.Close();
    assert(books.BeginManagement() == ESP_OK);
    assert(books.BeginEdgePageUpload(&upload) == storage::BookWriteResult::Ok);
    page = Page(2);
    assert(upload.Write(page.data(), 100) == storage::BookWriteResult::Ok);
    assert(upload.Commit() == storage::BookWriteResult::Invalid);
    upload.Abort();
    assert(books.EndManagement() == ESP_OK);
    assert(books.OpenEdgePage(&file) == ESP_OK && file.Read(0, header.data(), header.size()));
    assert(storage::edge::Decode(header.data(), file.Size(), &info) && info.revision == 1);
    file.Close();
    assert(books.BeginManagement() == ESP_OK);
    assert(books.BeginEdgePageUpload(&upload) == storage::BookWriteResult::Ok);
    assert(upload.Write(page.data(), page.size()) == storage::BookWriteResult::Ok);
    assert(upload.Commit() == storage::BookWriteResult::Ok);
    assert(books.EndManagement() == ESP_OK);
    assert(books.OpenEdgePage(&file) == ESP_OK && file.Read(0, header.data(), header.size()));
    assert(storage::edge::Decode(header.data(), file.Size(), &info) && info.revision == 2);
    file.Close();
    assert(books.BeginManagement() == ESP_OK);
    assert(books.BeginEdgePageUpload(&upload) == storage::BookWriteResult::Ok);
    page[28] = 1;
    assert(upload.Write(page.data(), page.size()) == storage::BookWriteResult::Ok);
    assert(upload.Commit() == storage::BookWriteResult::Invalid);
    upload.Abort(); assert(books.EndManagement() == ESP_OK);
    assert(books.OpenEdgePage(&file) == ESP_OK && file.Read(0, header.data(), header.size()));
    assert(storage::edge::Decode(header.data(), file.Size(), &info) && info.revision == 2);
}
}
int main(int argc, char** argv) { assert(argc == 2); TransferTests(); CacheTests(argv[1]); }
