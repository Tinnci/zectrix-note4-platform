#pragma once

#include <atomic>
#include "zectrix_book_transfer.h"

namespace zectrix::connectivity {

enum class BookHttpMethod : uint8_t { Get, Put, Delete, Post, Other };

class BookHttpRequest {
public:
    virtual ~BookHttpRequest() = default;
    BookHttpMethod method = BookHttpMethod::Other;
    const char* uri = "";
    std::size_t content_length = 0;
    virtual bool Header(const char* name, char* value, std::size_t capacity) = 0;
    virtual int Read(uint8_t* output, std::size_t capacity) = 0;
    virtual bool Respond(int status, const char* type, const char* attachment = nullptr) = 0;
    virtual bool Write(const char* data, std::size_t size) = 0;
    virtual bool Finish() = 0;
    virtual uint32_t NowMs() const = 0;
};

// The HTTP task owns requests and file handles. Other tasks only cancel or read progress.
class BookWebApi {
public:
    void Start(storage::BookStorage& books, const char* code, uint32_t now_ms);
    void Cancel() { cancelled_.store(true); }
    bool Cancelled() const { return cancelled_.load(); }
    bool Handle(BookHttpRequest& request);
    BookTransferProgress Progress() const;

private:
    bool List(BookHttpRequest& request, const char* after, bool application);
    bool Upload(BookHttpRequest& request, const char* name, bool application);
    bool Download(BookHttpRequest& request, const char* name, bool application);
    bool Expired(const BookHttpRequest& request, uint32_t started) const;
    storage::BookStorage* books_ = nullptr;
    std::array<char, 20> authorization_{};
    std::atomic<bool> cancelled_{true}, active_{false}, finish_{false};
    std::atomic<uint32_t> received_{0}, expected_{0}, uploaded_{0}, activity_{0}, completed_{0};
};

class EspBookWebServer final : public BookTransferServer {
public:
    EspBookWebServer();
    ~EspBookWebServer() override;
    bool Start(storage::BookStorage& books, const char* code, uint32_t now_ms) override;
    BookTransferProgress Progress() const override;
    bool Stop() override;
private:
    struct Impl;
    Impl* impl_ = nullptr;
};

}  // namespace zectrix::connectivity
