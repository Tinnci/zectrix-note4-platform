#include "zectrix_book_transfer.h"
#include "zectrix_book_storage.h"

#include <algorithm>
#include <cstring>

namespace zectrix::connectivity {

bool BookTransfer::Begin(storage::BookStorage& books, BookTransferMode mode,
                         const WifiCredentials& credentials, const char* code, uint32_t now_ms) {
    if (Busy() || !code || std::strlen(code) != 12) return false;
    snapshot_ = {};
    snapshot_.mode = mode;
    snapshot_.ssid = credentials.ssid;
    std::strcpy(snapshot_.code.data(), code);
    snapshot_.state = BookTransferState::Starting;
    books_ = &books;
    started_ms_ = now_ms;
    finishing_ = false;
    server_started_ = false;
    pending_credentials_ = credentials;
    start_pending_ = true;
    return true;
}

void BookTransfer::Poll(uint32_t now_ms, bool power_allowed, bool policy_allowed) {
    if (!Busy()) return;
    if (!power_allowed || !policy_allowed || snapshot_.state == BookTransferState::Stopping) {
        Stop(!power_allowed ? BookTransferError::Power : !policy_allowed ? BookTransferError::Policy : snapshot_.error);
        return;
    }
    if (now_ms - started_ms_ >= kSessionMs) { Stop(BookTransferError::Timeout); return; }
    if (snapshot_.state == BookTransferState::Starting && now_ms - started_ms_ >= kStartupMs) {
        Stop(BookTransferError::Timeout); return;
    }
    if (start_pending_) {
        start_pending_ = false;
        const auto result = radio_.Start(snapshot_.mode, pending_credentials_);
        ClearWifiCredentials(&pending_credentials_);
        if (result != WifiDriverResult::kReady && result != WifiDriverResult::kPending) { Stop(BookTransferError::Wifi); return; }
    }
    const auto link = radio_.Poll(snapshot_.mode, snapshot_.address.data(), snapshot_.address.size());
    if (link != WifiDriverResult::kReady && link != WifiDriverResult::kPending) {
        Stop(BookTransferError::Wifi); return;
    }
    if (snapshot_.state == BookTransferState::Starting) {
        if (link != WifiDriverResult::kReady) return;
        if (!server_.Start(*books_, snapshot_.code.data(), now_ms)) { Stop(BookTransferError::Server); return; }
        server_started_ = true;
        snapshot_.state = BookTransferState::Sharing;
    } else if (link != WifiDriverResult::kReady) { Stop(BookTransferError::Wifi); return; }
    const auto progress = server_.Progress();
    snapshot_.expected = progress.expected;
    snapshot_.received = std::min(progress.received, progress.expected);
    snapshot_.uploaded = progress.uploaded;
    if (progress.finish && !finishing_) { finishing_ = true; finish_ms_ = now_ms; }
    if (finishing_) {
        // Give the final HTTP response time to leave the radio before stopping it.
        if (now_ms - finish_ms_ >= 500) Stop();
        return;
    }
    // The HTTP task can publish activity just after this poll captures its time.
    // Sessions are shorter than half the clock range, so a future sample is idle for zero milliseconds.
    const uint32_t elapsed = now_ms - progress.activity_ms;
    const uint32_t idle = (elapsed & 0x80000000u) ? 0 : elapsed;
    uint32_t remaining = idle < kIdleMs ? kIdleMs - idle : 0;
    if (progress.uploaded && !progress.active) {
        remaining = std::min(remaining, idle < kAfterUploadMs ? kAfterUploadMs - idle : 0);
    }
    snapshot_.seconds_left = (std::min(remaining, kSessionMs - (now_ms - started_ms_)) + 999) / 1000;
    if (!remaining && !progress.active) Stop();
}

bool BookTransfer::Stop(BookTransferError error) {
    if (!Busy()) return true;
    start_pending_ = false;
    ClearWifiCredentials(&pending_credentials_);
    snapshot_.state = BookTransferState::Stopping;
    if (error != BookTransferError::None) snapshot_.error = error;
    if (!server_.Stop()) {
        snapshot_.error = BookTransferError::Stop;
        return false;
    }
    if (server_started_) {
        const auto final = server_.Progress();
        snapshot_.received = final.received;
        snapshot_.expected = final.expected;
        snapshot_.uploaded = final.uploaded;
        server_started_ = false;
    }
    if (radio_.Stop() != WifiDriverResult::kReady) {
        snapshot_.error = BookTransferError::Stop;
        return false;
    }
    if (books_->EndManagement() != ESP_OK) {
        snapshot_.error = BookTransferError::Storage;
        return false;
    }
    books_ = nullptr;
    snapshot_.state = snapshot_.error == BookTransferError::None ? BookTransferState::Complete : BookTransferState::Failed;
    snapshot_.code.fill(0);
    snapshot_.address.fill(0);
    return true;
}

}  // namespace zectrix::connectivity
