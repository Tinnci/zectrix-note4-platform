#include "zectrix_reader_platform.h"
#include "sdkconfig.h"
#if CONFIG_ZECTRIX_ENABLE_CONNECTIVITY
#include "zectrix_connectivity_service.h"
#endif

#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace {
using namespace zectrix;
std::vector<uint8_t> local_record;
bool fail_local_save = false;
storage::BookStorage* files = nullptr;
#if CONFIG_ZECTRIX_ENABLE_CONNECTIVITY
companion::SyncEngine* sync = nullptr;

class MemorySyncStore final : public companion::SyncStore {
public:
    std::vector<uint8_t> record;
    bool fail = false;
    unsigned writes = 0;
    companion::StoreReadStatus Load(uint8_t* output, std::size_t capacity, std::size_t* size) override {
        if (record.empty()) return companion::StoreReadStatus::kNotFound;
        assert(record.size() <= capacity);
        *size = record.size();
        std::memcpy(output, record.data(), *size);
        return companion::StoreReadStatus::kOk;
    }
    bool Save(const uint8_t* input, std::size_t size) override {
        if (fail) return false;
        record.assign(input, input + size); ++writes; return true;
    }
};
#endif
}

namespace zectrix::storage {
esp_err_t StorageService::Create(StorageService** output) { *output = new StorageService(nullptr); return ESP_OK; }
StorageService::~StorageService() = default;
esp_err_t StorageService::GetBlob(const char* key, void* value, std::size_t* size) const {
    assert(std::strcmp(key, "reader.marks") == 0);
    if (local_record.empty()) return ESP_ERR_NOT_FOUND;
    if (!value) { *size = local_record.size(); return ESP_OK; }
    if (*size < local_record.size()) return ESP_ERR_INVALID_SIZE;
    *size = local_record.size(); std::memcpy(value, local_record.data(), *size); return ESP_OK;
}
esp_err_t StorageService::SetBlob(const char* key, const void* value, std::size_t size) {
    assert(std::strcmp(key, "reader.marks") == 0);
    if (fail_local_save) return ESP_FAIL;
    const auto* bytes = static_cast<const uint8_t*>(value);
    local_record.assign(bytes, bytes + size); return ESP_OK;
}
esp_err_t StorageService::ListBooks(BookEntry* entries, std::size_t capacity, std::size_t* count, bool* truncated) {
    return files->List(entries, capacity, count, truncated);
}
esp_err_t StorageService::OpenBook(const char* name, BookFile* file) { return files->Open(name, file); }
}

#if CONFIG_ZECTRIX_ENABLE_CONNECTIVITY
namespace zectrix::connectivity {
ConnectivityResult ConnectivityService::Create(ConnectivityService** output) {
    *output = new ConnectivityService(nullptr); return ConnectivityResult::kOk;
}
ConnectivityService::~ConnectivityService() = default;
companion::SyncStatus ConnectivityService::PutDurableState(
    uint16_t key, uint32_t revision, const uint8_t* value, std::size_t size) {
    return sync->PutDurableState(key, revision, value, size);
}
companion::SyncStatus ConnectivityService::ReadDurableState(
    uint16_t key, uint32_t* revision, uint8_t* value, std::size_t* size) const {
    companion::DurableStateView state;
    const auto result = sync->ReadIncomingState(key, &state);
    if (result != companion::SyncStatus::kOk) return result;
    if (*size < state.value_size) return companion::SyncStatus::kValueTooLarge;
    *size = state.value_size; *revision = state.revision;
    std::memcpy(value, state.value, *size);
    return result;
}
}

#endif

void TestReaderPlatform(const char* directory) {
    using namespace zectrix::reader;
    namespace fs = std::filesystem;
    const auto root = fs::path(directory) / "library";
    fs::create_directory(root);
    std::ofstream(root / "alpha.txt") << "第一段中文。 A real file source.\nNext paragraph.";
    fs::copy_file(fs::path(directory) / "deflated.epub", root / "日本語.EPUB");
    std::ofstream(root / "ignored.bin") << "ignored";
    fs::create_directory(root / "directory.txt");
    fs::create_symlink(root / "alpha.txt", root / "link.txt");
    storage::BookStorage file_store(root.c_str());
    files = &file_store;
    std::array<storage::BookEntry, 4> entries;
    std::size_t count = 0;
    bool truncated = false;
    assert(file_store.List(entries.data(), entries.size(), &count, &truncated) == ESP_OK);
    assert(count == 2 && !truncated && std::strcmp(entries[0].name.data(), "alpha.txt") == 0);
    assert(file_store.List(entries.data(), 1, &count, &truncated) == ESP_OK && count == 1 && truncated);
    storage::BookFile file;
    assert(file_store.Open("../alpha.txt", &file) == ESP_ERR_INVALID_ARG);
    assert(file_store.Open("link.txt", &file) == ESP_ERR_INVALID_SIZE);
    assert(file_store.Open("alpha.txt", &file) == ESP_OK);
    uint8_t bytes[4];
    assert(file.Read(0, bytes, sizeof(bytes)) && bytes[0] == 0xe7);
    assert(file.Read(12, bytes, sizeof(bytes)));
    assert(file.Read(0, bytes, sizeof(bytes)) && bytes[0] == 0xe7);
    assert(!file.Read(file.Size() - 1, bytes, sizeof(bytes)));
    assert(!file.Read(UINT32_MAX, bytes, sizeof(bytes)));
    file.Close();
    assert(!file.Read(0, bytes, 1));

    storage::StorageService* storage_service = nullptr;
    assert(storage::StorageService::Create(&storage_service) == ESP_OK);
    StorageLibrary library(*storage_service);
    assert(library.Refresh() == Result::Ok && library.count() == 2);
    assert(library.Get(1).format == Format::Epub);
    Source* source = nullptr;
    assert(library.Open(1, &source) == Result::Ok && source);
    Engine engine;
    Result result = engine.Open(*source, Format::Epub);
    while (result == Result::Pending) result = engine.Poll(64);
    assert(result == Result::Ok);
    assert(engine.Seek({}, FontSize::Small) == Result::Pending);
    do { result = engine.Poll(64); } while (result == Result::Pending);
    assert(result == Result::Ok && engine.page().glyphs[0].codepoint == U'第');
    engine.Close();
    library.Close();

#if CONFIG_ZECTRIX_ENABLE_CONNECTIVITY
    MemorySyncStore sync_store;
    companion::SyncEngine initial_sync;
    assert(initial_sync.Initialize(sync_store) == companion::SyncStatus::kOk);
    sync = &initial_sync;
    connectivity::ConnectivityService* connectivity_service = nullptr;
    assert(connectivity::ConnectivityService::Create(&connectivity_service) == connectivity::ConnectivityResult::kOk);
    PlatformBookmarkStore adapter(*storage_service, *connectivity_service);
    Bookmarks bookmarks(adapter);
    assert(bookmarks.Load() == Result::Ok);
    assert(!bookmarks.Latest());
    Bookmark mark;
    mark.book_id = library.Get(0).id;
    mark.source_bytes = library.Get(0).bytes;
    mark.position.offset = 12;
    assert(bookmarks.Save(mark) == Result::Ok);
    sync_store.fail = true;
    assert(bookmarks.Sync() == Result::IoError && bookmarks.pending_sync());

    // Reboot between the app snapshot and enqueue must replay the same value.
    companion::SyncEngine recovered_sync;
    sync_store.fail = false;
    assert(recovered_sync.Initialize(sync_store) == companion::SyncStatus::kOk);
    sync = &recovered_sync;
    Bookmarks reboot(adapter);
    assert(reboot.Load() == Result::Ok);
    assert(reboot.Latest() && *reboot.Latest() == mark);
    assert(reboot.Sync() == Result::Ok && !reboot.pending_sync());
    companion::DurableStateView pending;
    assert(sync->NextDurableState(&pending) == companion::SyncStatus::kOk);
    assert(pending.key == kProgressSyncKey && pending.revision == 1);
    Bookmark decoded;
    assert(DecodeBookmark(pending.value, pending.value_size, &decoded) && decoded == mark);
    assert(sync->AcknowledgeDurableState(kProgressSyncKey, 1) == companion::SyncStatus::kOk);
    const auto writes = sync_store.writes;
    Bookmarks after_ack(adapter);
    assert(after_ack.Load() == Result::Ok && after_ack.Sync() == Result::Ok);
    assert(!after_ack.pending_sync() && sync_store.writes == writes);

    mark.position.offset = 0;
    std::array<uint8_t, kBookmarkBytes> payload{};
    std::size_t size = 0;
    assert(EncodeBookmark(mark, payload.data(), payload.size(), &size));
    assert(sync->AcceptIncomingState({kProgressSyncKey, 1, payload.data(), size}) == companion::SyncStatus::kOk);
    assert(after_ack.Sync() == Result::Ok && after_ack.remote());
    assert(after_ack.Find("alpha.txt", mark.source_bytes)->position.offset == 12);
    fail_local_save = true;
    assert(after_ack.ApplyRemote() == Result::IoError && after_ack.remote());
    assert(after_ack.Latest() && after_ack.Latest()->position.offset == 12);
    fail_local_save = false;
    assert(after_ack.ApplyRemote() == Result::Ok && after_ack.Sync() == Result::Ok);
    assert(sync->NextDurableState(&pending) == companion::SyncStatus::kOk && pending.revision == 2);
    assert(DecodeBookmark(pending.value, pending.value_size, &decoded) && decoded == mark);
    Bookmarks again(adapter);
    assert(again.Load() == Result::Ok && again.Sync() == Result::Ok && !again.remote());
    assert(*again.Find("alpha.txt", mark.source_bytes) == mark);
    // Offline progress survives a later switch back to companion firmware.
    PlatformBookmarkStore offline(*storage_service);
    Bookmarks offline_reader(offline);
    assert(offline_reader.Load() == Result::Ok);
    mark.position.offset = 8;
    assert(offline_reader.Save(mark) == Result::Ok);
    assert(offline_reader.Sync() == Result::Pending && offline_reader.pending_sync());
    Bookmarks reconnected(adapter);
    assert(reconnected.Load() == Result::Ok && *reconnected.Latest() == mark);
    assert(reconnected.Sync() == Result::Ok && !reconnected.pending_sync());
    assert(sync->NextDurableState(&pending) == companion::SyncStatus::kOk);
    assert(DecodeBookmark(pending.value, pending.value_size, &decoded) && decoded == mark);
    local_record.assign(kBookmarkStoreBytes + 1, 1);
    assert(adapter.Load(payload.data(), payload.size(), &size) == Result::TooLarge);
    delete connectivity_service;
    sync = nullptr;
#else
    PlatformBookmarkStore adapter(*storage_service);
    Bookmarks bookmarks(adapter);
    assert(bookmarks.Load() == Result::Ok && !bookmarks.Latest());
    Bookmark mark;
    mark.book_id = library.Get(0).id;
    mark.source_bytes = library.Get(0).bytes;
    mark.position.offset = 12;
    assert(bookmarks.Save(mark) == Result::Ok);
    assert(bookmarks.Sync() == Result::Pending && bookmarks.pending_sync());
    Bookmarks reboot(adapter);
    assert(reboot.Load() == Result::Ok && *reboot.Latest() == mark);
    assert(reboot.Sync() == Result::Pending && !reboot.remote());
    fail_local_save = true;
    mark.position.offset = 0;
    assert(reboot.Save(mark) == Result::IoError);
    assert(reboot.Latest()->position.offset == 12);
    fail_local_save = false;
#endif
    delete storage_service;
    files = nullptr;
}
