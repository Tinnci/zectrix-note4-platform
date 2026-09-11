#pragma once

#include "zectrix_book_storage.h"
#include "zectrix_host_channel.h"

namespace zectrix::host {

class Settings {
public:
    virtual ~Settings() = default;
    virtual Status Get(Setting key, uint32_t* value) = 0;
    virtual Status Set(Setting key, uint32_t value) = 0;
};

enum class TransferState : uint8_t { Waiting, Ready, Uploading, Downloading, Complete, Cancelled, Failed, Removed };
struct Snapshot {
    TransferState state = TransferState::Waiting;
    Status error = Status::Ok;
    std::array<char, 64> name{};
    uint32_t transferred = 0, expected = 0, uploaded = 0;
    uint32_t settings_revision = 0;
};

// All methods run on the foreground owner, after application entry and before
// exit. The USB worker sees only Channel's copied frames.
class BookSession final {
public:
    BookSession(Channel& channel, Settings& settings) : channel_(channel), settings_(settings) {}
    ~BookSession() { Stop(); }
    // Takes ownership of an already acquired StorageService management lease.
    void Start(storage::BookStorage& books);
    void Stop();
    void Cancel();
    void Poll();
    const Snapshot& snapshot() const { return snapshot_; }

private:
    void ResetTransfer();
    Status Execute();
    Status List(bool application = false);
    Status Open(bool upload, bool application = false);
    Status Write();
    Status Commit();
    Status Read();
    bool Name(std::size_t offset, char* output, bool application = false) const;
    bool Transferring() const;
    Channel& channel_;
    Settings& settings_;
    storage::BookStorage* books_ = nullptr;
    storage::BookFile file_;
    storage::BookUpload upload_;
    Frame request_{}, reply_{};
    Snapshot snapshot_{};
    uint32_t session_ = 0;
};

}  // namespace zectrix::host
