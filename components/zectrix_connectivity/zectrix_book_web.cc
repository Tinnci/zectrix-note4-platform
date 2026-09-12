#include "zectrix_book_web.h"
#include "zectrix_book_storage.h"
#include "zectrix_app_storage.h"
#include "sdkconfig.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

extern "C" const char zectrix_book_web_html[];

namespace zectrix::connectivity {
namespace {
#if CONFIG_ZECTRIX_ENABLE_RUNTIME
constexpr bool kAppsEnabled = true;
#else
constexpr bool kAppsEnabled = false;
#endif
bool Reply(BookHttpRequest& request, int status, const char* message) {
    return request.Respond(status, "application/json; charset=utf-8") &&
        request.Write(message, std::strlen(message)) && request.Finish();
}

bool WriteResult(BookHttpRequest& request, storage::BookWriteResult result, bool application = false) {
    using Result = storage::BookWriteResult;
    switch (result) {
        case Result::Ok: return Reply(request, 200, "{\"ok\":true}");
        case Result::Invalid: return Reply(request, 400, application ?
            "{\"error\":\"Invalid app name or package. Use a compatible .zapp or .lua file with a name of at most 47 UTF-8 bytes.\"}" :
            "{\"error\":\"Use a UTF-8 TXT or EPUB filename of at most 63 bytes.\"}");
        case Result::Busy: return Reply(request, 409, "{\"error\":\"Content storage is busy.\"}");
        case Result::Exists: return Reply(request, 409, "{\"error\":\"This name already exists. Rename the new file or delete the old file first.\"}");
        case Result::NotFound: return Reply(request, 404, "{\"error\":\"File not found.\"}");
        case Result::NoSpace: return Reply(request, 507, "{\"error\":\"Not enough storage. Delete a file and try again.\"}");
        case Result::IoError: return Reply(request, 500, "{\"error\":\"Storage failed. The upload was not installed.\"}");
    }
    return false;
}

int Hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool DecodeName(const char* encoded, char* output, bool application) {
    std::size_t used = 0;
    while (*encoded) {
        if (used == 63) return false;
        uint8_t value = static_cast<uint8_t>(*encoded++);
        if (value == '%') {
            if (!encoded[0] || !encoded[1] || Hex(encoded[0]) < 0 || Hex(encoded[1]) < 0) return false;
            value = Hex(encoded[0]) * 16 + Hex(encoded[1]);
            encoded += 2;
        }
        if (!value) return false;
        output[used++] = static_cast<char>(value);
    }
    output[used] = 0;
    return application ? storage::AppStorage::ValidName(output) : storage::BookStorage::ValidName(output);
}

void JsonName(const char* input, char* output) {
    while (*input) {
        if (*input == '"' || *input == '\\') *output++ = '\\';
        *output++ = *input++;
    }
    *output = 0;
}
}  // namespace

void BookWebApi::Start(storage::BookStorage& books, const char* code, uint32_t now_ms) {
    books_ = &books;
    std::snprintf(authorization_.data(), authorization_.size(), "Bearer %s", code);
    received_ = expected_ = uploaded_ = completed_ = 0;
    activity_ = now_ms;
    active_ = finish_ = false;
    cancelled_ = false;
}

BookTransferProgress BookWebApi::Progress() const {
    return {received_.load(), expected_.load(), uploaded_.load(), activity_.load(), completed_.load(),
            active_.load(), finish_.load()};
}

bool BookWebApi::Expired(const BookHttpRequest& request, uint32_t started) const {
    return cancelled_.load() || request.NowMs() - started >= 120000;
}

bool BookWebApi::Handle(BookHttpRequest& request) {
    if (cancelled_.load() || finish_.load()) return Reply(request, 503, "{\"error\":\"This transfer session has ended.\"}");
    if (request.method == BookHttpMethod::Get && std::strcmp(request.uri, "/") == 0) {
        return request.Respond(200, "text/html; charset=utf-8") &&
            request.Write(zectrix_book_web_html, std::strlen(zectrix_book_web_html)) && request.Finish();
    }
    std::array<char, 20> authorization{};
    if (!request.Header("Authorization", authorization.data(), authorization.size()))
        return Reply(request, 401, "{\"error\":\"Enter the access code shown on your Note4.\"}");
    unsigned mismatch = 0;
    for (std::size_t i = 0; i < authorization.size(); ++i) mismatch |= authorization[i] ^ authorization_[i];
    if (mismatch) return Reply(request, 401, "{\"error\":\"The access code does not match.\"}");
    if (active_.exchange(true)) return Reply(request, 409, "{\"error\":\"Another file operation is running.\"}");
    struct ActiveRequest {
        std::atomic<bool>& active;
        ~ActiveRequest() { active.store(false); }
    } active{active_};
    activity_ = request.NowMs();
    if (request.method == BookHttpMethod::Post && std::strcmp(request.uri, "/api/finish") == 0 && !request.content_length) {
        const bool sent = Reply(request, 200, "{\"ok\":true}");
        finish_ = true;
        return sent;
    }
    const bool application = std::strncmp(request.uri, "/api/apps", 9) == 0;
    if (application && !kAppsEnabled) return Reply(request, 503, "{\"error\":\"Apps are disabled in this firmware.\"}");
    const char* base = application ? "/api/apps" : "/api/books";
    const auto prefix = std::strlen(base);
    if (std::strncmp(request.uri, base, prefix) != 0) return Reply(request, 404, "{\"error\":\"Page not found.\"}");
    const char* tail = request.uri + prefix;
    char name[64]{};
    if (request.method == BookHttpMethod::Get) {
        if (!*tail) return List(request, nullptr, application);
        if (std::strncmp(tail, "?after=", 7) == 0) {
            if (!DecodeName(tail + 7, name, application)) return WriteResult(request, storage::BookWriteResult::Invalid, application);
            return List(request, name, application);
        }
    }
    if (*tail != '/') return Reply(request, 404, "{\"error\":\"Page not found.\"}");
    if (!DecodeName(tail + 1, name, application)) return WriteResult(request, storage::BookWriteResult::Invalid, application);
    switch (request.method) {
        case BookHttpMethod::Put: return Upload(request, name, application);
        case BookHttpMethod::Get: return Download(request, name, application);
        case BookHttpMethod::Delete:
            if (request.content_length) return Reply(request, 400, "{\"error\":\"Unexpected request body.\"}");
            return WriteResult(request, application ? storage::AppStorage(*books_).Remove(name) : books_->Remove(name), application);
        default: return Reply(request, 405, "{\"error\":\"Method not allowed.\"}");
    }
}

bool BookWebApi::List(BookHttpRequest& request, const char* after, bool application) {
    std::array<storage::BookEntry, 32> books{};
    std::size_t count = 0;
    bool more = false;
    storage::BookSpace space;
    const auto listed = application ? storage::AppStorage(*books_).List(books.data(), books.size(), &count, &more, after)
                                   : books_->List(books.data(), books.size(), &count, &more, after);
    if (listed != ESP_OK || books_->Space(&space) != ESP_OK)
        return WriteResult(request, storage::BookWriteResult::IoError);
    if (!request.Respond(200, "application/json; charset=utf-8") || !request.Write("{\"files\":[", 10)) return false;
    char buffer[256], name[128];
    for (std::size_t i = 0; i < count; ++i) {
        JsonName(books[i].name.data(), name);
        const int size = std::snprintf(buffer, sizeof(buffer), "%s{\"name\":\"%s\",\"size\":%lu}",
            i ? "," : "", name, static_cast<unsigned long>(books[i].size));
        if (!request.Write(buffer, size)) return false;
    }
    const int size = std::snprintf(buffer, sizeof(buffer), "],\"more\":%s,\"available\":%lu,\"total\":%lu,\"used\":%lu,\"apps_supported\":%s}",
        more ? "true" : "false", static_cast<unsigned long>(space.available),
        static_cast<unsigned long>(space.total), static_cast<unsigned long>(space.used), kAppsEnabled ? "true" : "false");
    return request.Write(buffer, size) && request.Finish();
}

bool BookWebApi::Upload(BookHttpRequest& request, const char* name, bool application) {
    const auto limit = application ? storage::AppStorage::SizeLimit(name) : 0x400000;
    if (request.content_length > limit) return Reply(request, 413, "{\"error\":\"This file exceeds its Note4 size limit.\"}");
    storage::BookUpload upload;
    const auto begun = application ? storage::AppStorage(*books_).BeginUpload(name, request.content_length, &upload)
                                   : books_->BeginUpload(name, request.content_length, &upload);
    if (begun != storage::BookWriteResult::Ok) return WriteResult(request, begun, application);
    received_ = 0;
    expected_ = request.content_length;
    const auto started = request.NowMs();
    std::array<uint8_t, 1024> buffer{};
    std::size_t remaining = request.content_length;
    while (remaining) {
        if (Expired(request, started)) return Reply(request, 408, "{\"error\":\"Upload cancelled or timed out.\"}");
        const auto capacity = std::min(buffer.size(), remaining);
        const int read = request.Read(buffer.data(), capacity);
        if (read <= 0 || static_cast<std::size_t>(read) > capacity)
            return Reply(request, 408, "{\"error\":\"Upload interrupted. Please send the file again.\"}");
        const auto written = upload.Write(buffer.data(), read);
        if (written != storage::BookWriteResult::Ok) return WriteResult(request, written, application);
        remaining -= read;
        received_ = request.content_length - remaining;
        activity_ = request.NowMs();
    }
    if (Expired(request, started)) return Reply(request, 408, "{\"error\":\"Upload cancelled or timed out.\"}");
    const auto committed = upload.Commit();
    if (committed == storage::BookWriteResult::Ok) {
        completed_ = request.NowMs();
        activity_ = completed_.load();
        ++uploaded_;
    }
    return WriteResult(request, committed, application);
}

bool BookWebApi::Download(BookHttpRequest& request, const char* name, bool application) {
    storage::BookFile file;
    const auto opened = application ? storage::AppStorage(*books_).OpenManaged(name, &file) : books_->OpenManaged(name, &file);
    if (opened != ESP_OK) return WriteResult(request, opened == ESP_ERR_NOT_FOUND ?
        storage::BookWriteResult::NotFound : storage::BookWriteResult::IoError);
    if (!request.Respond(200, "application/octet-stream", name)) return false;
    std::array<uint8_t, 1024> buffer{};
    const auto started = request.NowMs();
    for (uint32_t offset = 0; offset < file.Size();) {
        const auto size = std::min<std::size_t>(buffer.size(), file.Size() - offset);
        if (Expired(request, started) || !file.Read(offset, buffer.data(), size) ||
            !request.Write(reinterpret_cast<const char*>(buffer.data()), size)) return false;
        offset += size;
        activity_ = request.NowMs();
    }
    return request.Finish();
}

}  // namespace zectrix::connectivity
