#include "zectrix_book_web.h"
#include "zectrix_book_storage.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

extern "C" const char zectrix_book_web_html[];

namespace zectrix::connectivity {
namespace {
bool Reply(BookHttpRequest& request, int status, const char* message) {
    return request.Respond(status, "application/json; charset=utf-8") &&
        request.Write(message, std::strlen(message)) && request.Finish();
}

bool WriteResult(BookHttpRequest& request, storage::BookWriteResult result) {
    using Result = storage::BookWriteResult;
    switch (result) {
        case Result::Ok: return Reply(request, 200, "{\"ok\":true}");
        case Result::Invalid: return Reply(request, 400, "{\"error\":\"Use a UTF-8 TXT or EPUB filename of at most 63 bytes.\"}");
        case Result::Busy: return Reply(request, 409, "{\"error\":\"Book storage is busy.\"}");
        case Result::Exists: return Reply(request, 409, "{\"error\":\"This name already exists. Rename the new file or delete the old book first.\"}");
        case Result::NotFound: return Reply(request, 404, "{\"error\":\"Book not found.\"}");
        case Result::NoSpace: return Reply(request, 507, "{\"error\":\"Not enough book storage. Delete a book and try again.\"}");
        case Result::IoError: return Reply(request, 500, "{\"error\":\"Book storage failed. The upload was not installed.\"}");
    }
    return false;
}

int Hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool DecodeName(const char* encoded, char* output) {
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
    return storage::BookStorage::ValidName(output);
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
    constexpr char listing[] = "/api/books", page[] = "/api/books?after=", file[] = "/api/books/";
    char name[64]{};
    if (request.method == BookHttpMethod::Get) {
        if (std::strcmp(request.uri, listing) == 0) return List(request, nullptr);
        if (std::strncmp(request.uri, page, sizeof(page) - 1) == 0 && DecodeName(request.uri + sizeof(page) - 1, name))
            return List(request, name);
    }
    if (std::strncmp(request.uri, file, sizeof(file) - 1) != 0)
        return Reply(request, 404, "{\"error\":\"Page not found.\"}");
    if (!DecodeName(request.uri + sizeof(file) - 1, name)) return WriteResult(request, storage::BookWriteResult::Invalid);
    switch (request.method) {
        case BookHttpMethod::Put: return Upload(request, name);
        case BookHttpMethod::Get: return Download(request, name);
        case BookHttpMethod::Delete:
            if (request.content_length) return Reply(request, 400, "{\"error\":\"Unexpected request body.\"}");
            return WriteResult(request, books_->Remove(name));
        default: return Reply(request, 405, "{\"error\":\"Method not allowed.\"}");
    }
}

bool BookWebApi::List(BookHttpRequest& request, const char* after) {
    std::array<storage::BookEntry, 32> books{};
    std::size_t count = 0;
    bool more = false;
    storage::BookSpace space;
    if (books_->List(books.data(), books.size(), &count, &more, after) != ESP_OK || books_->Space(&space) != ESP_OK)
        return WriteResult(request, storage::BookWriteResult::IoError);
    if (!request.Respond(200, "application/json; charset=utf-8") || !request.Write("{\"files\":[", 10)) return false;
    char buffer[256], name[128];
    for (std::size_t i = 0; i < count; ++i) {
        JsonName(books[i].name.data(), name);
        const int size = std::snprintf(buffer, sizeof(buffer), "%s{\"name\":\"%s\",\"size\":%lu}",
            i ? "," : "", name, static_cast<unsigned long>(books[i].size));
        if (!request.Write(buffer, size)) return false;
    }
    const int size = std::snprintf(buffer, sizeof(buffer), "],\"more\":%s,\"available\":%lu,\"total\":%lu,\"used\":%lu}",
        more ? "true" : "false", static_cast<unsigned long>(space.available),
        static_cast<unsigned long>(space.total), static_cast<unsigned long>(space.used));
    return request.Write(buffer, size) && request.Finish();
}

bool BookWebApi::Upload(BookHttpRequest& request, const char* name) {
    if (request.content_length > 0x400000) return Reply(request, 413, "{\"error\":\"This book is too large for Note4 storage.\"}");
    storage::BookUpload upload;
    const auto begun = books_->BeginUpload(name, request.content_length, &upload);
    if (begun != storage::BookWriteResult::Ok) return WriteResult(request, begun);
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
            return Reply(request, 408, "{\"error\":\"Upload interrupted. Please send the book again.\"}");
        const auto written = upload.Write(buffer.data(), read);
        if (written != storage::BookWriteResult::Ok) return WriteResult(request, written);
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
    return WriteResult(request, committed);
}

bool BookWebApi::Download(BookHttpRequest& request, const char* name) {
    storage::BookFile file;
    const auto opened = books_->OpenManaged(name, &file);
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
