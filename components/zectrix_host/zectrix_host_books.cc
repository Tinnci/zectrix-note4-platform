#include "zectrix_host_books.h"

#include <algorithm>
#include <cstring>
#include "sdkconfig.h"
#include "zectrix_app_storage.h"

namespace zectrix::host {
namespace {
Status Map(esp_err_t result) {
    if (result == ESP_OK) return Status::Ok;
    if (result == ESP_ERR_NOT_FOUND) return Status::NotFound;
    if (result == ESP_ERR_INVALID_ARG || result == ESP_ERR_INVALID_SIZE) return Status::Invalid;
    if (result == ESP_ERR_INVALID_STATE) return Status::Busy;
    return Status::IoError;
}
Status Map(storage::BookWriteResult result) {
    using R = storage::BookWriteResult;
    switch (result) {
        case R::Ok: return Status::Ok;
        case R::Invalid: return Status::Invalid;
        case R::Busy: return Status::Busy;
        case R::Exists: return Status::Exists;
        case R::NotFound: return Status::NotFound;
        case R::NoSpace: return Status::NoSpace;
        case R::IoError: return Status::IoError;
    }
    return Status::IoError;
}
}

void BookSession::Start(storage::BookStorage& books) {
    books_ = &books;
    snapshot_ = {};
    session_ = 0;
    channel_.Enable();
}
void BookSession::ResetTransfer() {
    upload_.Abort();
    file_.Close();
}
void BookSession::Stop() {
    if (!books_) return;
    channel_.Disable();
    ResetTransfer();
    if (books_) books_->EndManagement();
    books_ = nullptr;
    session_ = 0;
}
void BookSession::Cancel() {
    channel_.Disconnect(channel_.Session());
    ResetTransfer();
    session_ = 0;
    snapshot_.state = TransferState::Cancelled;
    snapshot_.error = Status::Cancelled;
}
bool BookSession::Transferring() const {
    return snapshot_.state == TransferState::Uploading || snapshot_.state == TransferState::Downloading;
}

void BookSession::Poll() {
    if (!books_) return;
    const auto current = channel_.Session();
    if (session_ != current) {
        ResetTransfer();
        snapshot_.state = current ? TransferState::Ready : TransferState::Waiting;
        snapshot_.error = Status::Ok;
        session_ = current;
    }
    if (!channel_.TakeRequest(&request_)) return;
    // A connection can arrive between the snapshot above and the slot take.
    if (session_ != request_.session) {
        ResetTransfer();
        snapshot_.state = TransferState::Ready;
        snapshot_.error = Status::Ok;
        session_ = request_.session;
    }
    reply_.operation = request_.operation | 0x80;
    reply_.session = request_.session;
    reply_.id = request_.id;
    reply_.size = 0;
    reply_.status = Execute();
    if (reply_.status != Status::Ok) snapshot_.error = reply_.status;
    channel_.Complete(reply_);
}

bool BookSession::Name(std::size_t offset, char* output, bool application) const {
    if (request_.size <= offset || request_.size - offset > 63) return false;
    const auto size = request_.size - offset;
    if (std::memchr(request_.payload.data() + offset, 0, size)) return false;
    std::memcpy(output, request_.payload.data() + offset, size);
    output[size] = 0;
    return application ? storage::AppStorage::ValidName(output) : storage::BookStorage::ValidName(output);
}

Status BookSession::List(bool application) {
    char after[64]{};
    if (request_.size && !Name(0, after, application)) return Status::Invalid;
    storage::BookEntry entries[8];
    std::size_t count = 0;
    bool more = false;
    const auto result = Map(application
        ? storage::AppStorage(*books_).List(entries, std::size(entries), &count, &more, after[0] ? after : nullptr)
        : books_->List(entries, std::size(entries), &count, &more, after[0] ? after : nullptr));
    if (result != Status::Ok) return result;
    reply_.payload[0] = static_cast<uint8_t>(count);
    reply_.payload[1] = more ? 1 : 0;
    reply_.size = 2;
    for (std::size_t i = 0; i < count; ++i) {
        const auto size = std::strlen(entries[i].name.data());
        auto* entry = reply_.payload.data() + reply_.size;
        entry[0] = static_cast<uint8_t>(size);
        Write32(entry + 1, entries[i].size);
        std::memcpy(entry + 5, entries[i].name.data(), size);
        reply_.size += 5 + size;
    }
    return Status::Ok;
}

Status BookSession::Open(bool upload, bool application) {
    if (Transferring()) return Status::Busy;
    char name[64];
    if (!Name(upload ? 4 : 0, name, application)) return Status::Invalid;
    storage::AppStorage apps(*books_);
    const auto result = upload
        ? Map(application ? apps.BeginUpload(name, Read32(request_.payload.data()), &upload_)
                          : books_->BeginUpload(name, Read32(request_.payload.data()), &upload_))
        : Map(application ? apps.OpenManaged(name, &file_) : books_->OpenManaged(name, &file_));
    if (result != Status::Ok) return result;
    std::strcpy(snapshot_.name.data(), name);
    snapshot_.transferred = 0;
    snapshot_.expected = upload ? Read32(request_.payload.data()) : file_.Size();
    snapshot_.state = upload ? TransferState::Uploading : TransferState::Downloading;
    snapshot_.error = Status::Ok;
    if (!upload) {
        Write32(reply_.payload.data(), snapshot_.expected);
        reply_.size = 4;
        if (snapshot_.expected == 0) {
            file_.Close();
            snapshot_.state = TransferState::Complete;
        }
    }
    return Status::Ok;
}

Status BookSession::Write() {
    if (snapshot_.state != TransferState::Uploading) return Status::Busy;
    Status result = Status::Invalid;
    if (request_.size > 4 && Read32(request_.payload.data()) == snapshot_.transferred) {
        result = Map(upload_.Write(request_.payload.data() + 4, request_.size - 4));
    }
    if (result != Status::Ok) {
        ResetTransfer();
        snapshot_.state = TransferState::Failed;
        return result;
    }
    snapshot_.transferred += request_.size - 4;
    Write32(reply_.payload.data(), snapshot_.transferred);
    reply_.size = 4;
    return Status::Ok;
}

Status BookSession::Commit() {
    if (snapshot_.state != TransferState::Uploading) return Status::Busy;
    const auto result = request_.size ? Status::Invalid : Map(upload_.Commit());
    ResetTransfer();
    snapshot_.state = result == Status::Ok ? TransferState::Complete : TransferState::Failed;
    if (result == Status::Ok) ++snapshot_.uploaded;
    return result;
}

Status BookSession::Read() {
    if (snapshot_.state != TransferState::Downloading) return Status::Busy;
    if (request_.size != 4 || Read32(request_.payload.data()) != snapshot_.transferred) return Status::Invalid;
    const auto size = std::min<uint32_t>(kPayloadSize, snapshot_.expected - snapshot_.transferred);
    if (!file_.Read(snapshot_.transferred, reply_.payload.data(), size)) {
        ResetTransfer();
        snapshot_.state = TransferState::Failed;
        return Status::IoError;
    }
    snapshot_.transferred += size;
    reply_.size = size;
    if (snapshot_.transferred == snapshot_.expected) {
        file_.Close();
        snapshot_.state = TransferState::Complete;
    }
    return Status::Ok;
}

Status BookSession::Execute() {
    if (request_.size > kPayloadSize) return Status::Invalid;
    switch (static_cast<Operation>(request_.operation)) {
        case Operation::Info: {
            if (request_.size) return Status::Invalid;
            storage::BookSpace space;
            const auto result = Map(books_->Space(&space));
            if (result != Status::Ok) return result;
            Write32(reply_.payload.data(), space.total);
            Write32(reply_.payload.data() + 4, space.used);
            Write32(reply_.payload.data() + 8, space.available);
            Write32(reply_.payload.data() + 12, kChunkSize);
            reply_.size = 16;
            return Status::Ok;
        }
        case Operation::List: return List();
        case Operation::ReadOpen: return Open(false);
        case Operation::Read: return Read();
        case Operation::UploadBegin: return Open(true);
        case Operation::UploadChunk: return Write();
        case Operation::UploadCommit: return Commit();
        case Operation::AppList:
        case Operation::AppReadOpen:
        case Operation::AppUploadBegin:
        case Operation::AppRemove:
#if CONFIG_ZECTRIX_ENABLE_RUNTIME
            if (request_.operation == static_cast<uint8_t>(Operation::AppList)) return List(true);
            if (request_.operation == static_cast<uint8_t>(Operation::AppReadOpen)) return Open(false, true);
            if (request_.operation == static_cast<uint8_t>(Operation::AppUploadBegin)) return Open(true, true);
            if (Transferring()) return Status::Busy;
            {
                char name[64];
                if (!Name(0, name, true)) return Status::Invalid;
                const auto result = Map(storage::AppStorage(*books_).Remove(name));
                if (result == Status::Ok) {
                    std::strcpy(snapshot_.name.data(), name);
                    snapshot_.state = TransferState::Removed;
                    snapshot_.error = Status::Ok;
                }
                return result;
            }
#else
            return Status::Unavailable;
#endif
        case Operation::Abort:
            if (request_.size) return Status::Invalid;
            ResetTransfer();
            snapshot_.state = TransferState::Ready;
            snapshot_.error = Status::Ok;
            return Status::Ok;
        case Operation::GetSetting: {
            if (request_.size != 1 || request_.payload[0] > static_cast<uint8_t>(Setting::SleepCover)) return Status::Invalid;
            uint32_t value = 0;
            const auto result = settings_.Get(static_cast<Setting>(request_.payload[0]), &value);
            if (result != Status::Ok) return result;
            Write32(reply_.payload.data(), value);
            reply_.size = 4;
            return Status::Ok;
        }
        case Operation::SetSetting: {
            if (request_.size != 5 || request_.payload[0] > static_cast<uint8_t>(Setting::SleepCover)) return Status::Invalid;
            const auto result = settings_.Set(static_cast<Setting>(request_.payload[0]), Read32(request_.payload.data() + 1));
            snapshot_.error = result;
            if (result == Status::Ok || result == Status::NotSaved) ++snapshot_.settings_revision;
            return result;
        }
        case Operation::Close: return Status::Invalid;
    }
    return Status::Invalid;
}

}  // namespace zectrix::host
