#include "zectrix_runtime.h"

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <cstring>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

namespace zectrix::runtime {
namespace {
union Allocation { std::max_align_t alignment; std::size_t bytes; };

bool ValidText(const char* text, std::size_t size) {
    for (std::size_t i = 0; i < size; ++i) {
        const auto first = static_cast<uint8_t>(text[i]);
        if (first < 0x20 || first == 0x7f) return false;
        if (first < 0x80) continue;
        const unsigned extra = first >= 0xc2 && first <= 0xdf ? 1 :
            first >= 0xe0 && first <= 0xef ? 2 : first >= 0xf0 && first <= 0xf4 ? 3 : 0;
        if (!extra || extra >= size - i) return false;
        uint32_t cp = first & (extra == 1 ? 31 : extra == 2 ? 15 : 7);
        for (unsigned n = 0; n < extra; ++n) {
            const auto next = static_cast<uint8_t>(text[++i]);
            if ((next & 0xc0) != 0x80) return false;
            cp = (cp << 6) | (next & 63);
        }
        if (cp < (extra == 1 ? 0x80U : extra == 2 ? 0x800U : 0x10000U) ||
            cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
    }
    return true;
}
}

void* Engine::Allocate(void* context, void* pointer, std::size_t, std::size_t size) {
    auto& self = *static_cast<Engine*>(context);
    auto* old = pointer ? static_cast<Allocation*>(pointer) - 1 : nullptr;
    const auto previous = old ? old->bytes : 0;
    if (!size) {
        self.heap_.live -= previous;
#ifdef ESP_PLATFORM
        heap_caps_free(old);
#else
        std::free(old);
#endif
        return nullptr;
    }
    if (size > SIZE_MAX - sizeof(Allocation) ||
        size + sizeof(Allocation) > self.heap_limit_ - (self.heap_.live - previous)) {
        ++self.heap_.rejected;
        return nullptr;
    }
    const auto bytes = size + sizeof(Allocation);
#ifdef ESP_PLATFORM
    auto* allocation = static_cast<Allocation*>(heap_caps_realloc(old, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#else
    auto* allocation = static_cast<Allocation*>(std::realloc(old, bytes));
#endif
    if (!allocation) {
        // Lua requires shrinking allocations to succeed. Retain the larger
        // block if the allocator cannot shrink it, accounting for its real size.
        if (old && bytes <= previous) return pointer;
        ++self.heap_.rejected;
        return nullptr;
    }
    allocation->bytes = bytes;
    self.heap_.live = self.heap_.live - previous + bytes;
    self.heap_.peak = std::max(self.heap_.peak, self.heap_.live);
    return allocation + 1;
}

Engine& Engine::Self(lua_State* state) {
    void* context = nullptr;
    lua_getallocf(state, &context);
    return *static_cast<Engine*>(context);
}

void Engine::Budget(lua_State* state, lua_Debug*) {
    // A yield returns to the C owner instead of raising a catchable guest error.
    lua_yield(state, 0);
}

int Engine::Prepare(lua_State* state) {
    auto& self = Self(state);
    lua_createtable(state, 0, 16);
    const luaL_Reg functions[] = {{"text", Text}, {"rect", Rect}, {"fill", Fill}, {"exit", Exit}, {nullptr, nullptr}};
    luaL_setfuncs(state, functions, 0);
    lua_pushinteger(state, kWidth); lua_setfield(state, -2, "width");
    lua_pushinteger(state, kHeight); lua_setfield(state, -2, "height");
    lua_pushinteger(state, 1); lua_setfield(state, -2, "api");
    lua_pushinteger(state, static_cast<int>(Key::Up)); lua_setfield(state, -2, "UP");
    lua_pushinteger(state, static_cast<int>(Key::Down)); lua_setfield(state, -2, "DOWN");
    lua_pushinteger(state, static_cast<int>(Key::Ok)); lua_setfield(state, -2, "OK");
    struct StyleName { const char* name; sdk::TextStyle style; };
    constexpr StyleName styles[] = {
        {"REGULAR", sdk::TextStyle::Regular}, {"BOLD", sdk::TextStyle::Bold},
        {"ITALIC", sdk::TextStyle::Italic}, {"DIM", sdk::TextStyle::Dim},
        {"UNDERLINE", sdk::TextStyle::Underline}, {"KEYCAP", sdk::TextStyle::Keycap},
    };
    for (const auto& style : styles) {
        lua_pushinteger(state, static_cast<int>(style.style));
        lua_setfield(state, -2, style.name);
    }
    lua_setglobal(state, "note4");
    self.callback_ = lua_newthread(state);
    return 1;
}

int Engine::Invoke(lua_State* state) {
    auto& self = Self(state);
    const char* name = self.phase_ == Phase::Event ? "on_event" : "on_render";
    lua_getglobal(state, name);
    if (!lua_isfunction(state, -1)) return luaL_error(state, "missing %s function", name);
    if (self.phase_ == Phase::Event) lua_pushinteger(state, static_cast<int>(self.key_));
    lua_callk(state, self.phase_ == Phase::Event ? 1 : 0, 1, 0, Finish);
    return Finish(state, LUA_OK, 0);
}

int Engine::Finish(lua_State* state, int, std::intptr_t) {
    auto& self = Self(state);
    if (self.phase_ == Phase::Event) self.redraw_ = lua_toboolean(state, -1);
    return 0;
}

bool Engine::Start(const uint8_t* source, std::size_t size) {
    Stop();
    heap_ = {};
    error_ = Error::None;
    detail_[0] = 0;
    if (!source || !size || size > kSourceLimit) return Fail(Error::InvalidSource, "invalid script size");
    owner_ = lua_newstate(Allocate, this);
    if (!owner_) return Fail(Error::Memory, "script memory limit");
    phase_ = Phase::Initialize;
    lua_pushcfunction(owner_, Prepare);
    const int prepared = lua_pcall(owner_, 0, 1, 0);
    if (prepared != LUA_OK) {
        return Fail(prepared == LUA_ERRMEM ? Error::Memory : Error::Guest, "script initialization failed");
    }
    // The loader is protected internally. Binary Lua chunks are never accepted.
    const int loaded = luaL_loadbufferx(callback_, reinterpret_cast<const char*>(source), size, "app", "t");
    if (!Result(loaded)) return false;
    lua_sethook(callback_, Budget, LUA_MASKCOUNT, kInstructionLimit);
    int results = 0;
    if (!Result(lua_resume(callback_, owner_, 0, &results))) return false;
    phase_ = Phase::Idle;
    redraw_ = true;
    return true;
}

bool Engine::Run(Phase phase) {
    if (!active() || phase_ != Phase::Idle) return false;
    phase_ = phase;
    // The restricted environment has no metatables/finalizers or closeable host
    // userdata; resetting a finished callback cannot execute guest cleanup code.
    if (!Result(lua_resetthread(callback_))) return false;
    lua_settop(callback_, 0);
    lua_pushcfunction(callback_, Invoke);
    lua_sethook(callback_, Budget, LUA_MASKCOUNT, kInstructionLimit);
    int results = 0;
    if (!Result(lua_resume(callback_, owner_, 0, &results))) return false;
    phase_ = Phase::Idle;
    return true;
}

bool Engine::Handle(Key key) { key_ = key; redraw_ = false; return Run(Phase::Event); }
bool Engine::Draw() { frame_.count = frame_.text_size = 0; return Run(Phase::Render); }

bool Engine::Result(int status) {
    if (status == LUA_OK) return true;
    const auto error = status == LUA_YIELD ? Error::Instructions : status == LUA_ERRMEM ? Error::Memory :
        status == LUA_ERRSYNTAX ? Error::InvalidSource : error_ == Error::Drawing ? Error::Drawing : Error::Guest;
    // Error objects can be arbitrary values. Only copy an existing Lua string.
    const char* message = callback_ && lua_type(callback_, -1) == LUA_TSTRING ? lua_tostring(callback_, -1) : nullptr;
    return Fail(error, status == LUA_YIELD ? "script instruction limit" : message ? message : "script failed");
}

bool Engine::Fail(Error error, const char* detail) {
    error_ = error;
    const auto size = strnlen(detail, detail_.size() - 1);
    for (std::size_t i = 0; i < size; ++i) {
        const auto c = static_cast<uint8_t>(detail[i]);
        detail_[i] = c >= 0x20 && c < 0x7f ? static_cast<char>(c) : '?';
    }
    detail_[size] = 0;
    Close();
    return false;
}

void Engine::Close() {
    if (owner_) lua_close(owner_);
    owner_ = callback_ = nullptr;
    phase_ = Phase::Idle;
}
void Engine::Stop() { Close(); exit_ = redraw_ = false; frame_.count = frame_.text_size = 0; }

int Engine::Coordinate(lua_State* state, int index, int upper) {
    const auto value = luaL_checkinteger(state, index);
    if (value < 0 || value > upper) luaL_error(state, "drawing coordinate out of range");
    return static_cast<int>(value);
}

int Engine::Text(lua_State* state) {
    auto& self = Self(state);
    if (self.phase_ != Phase::Render) return luaL_error(state, "drawing outside on_render");
    const auto x = Coordinate(state, 1, kWidth - 1), y = Coordinate(state, 2, kHeight - 1);
    std::size_t size = 0;
    const char* text = luaL_checklstring(state, 3, &size);
    const auto scale = luaL_optinteger(state, 4, 1);
    const auto style = luaL_optinteger(state, 5, 0);
    if (size > 128 || !ValidText(text, size) || (scale != 1 && scale != 2) || style < 0 || style > 31 ||
        self.frame_.count == self.frame_.commands.size() || size + 1 > self.frame_.text.size() - self.frame_.text_size) {
        self.error_ = Error::Drawing;
        return luaL_error(state, "drawing limit or invalid text");
    }
    auto& command = self.frame_.commands[self.frame_.count++];
    command = {DrawKind::Text, static_cast<uint8_t>(scale), static_cast<uint16_t>(x), static_cast<uint16_t>(y),
               0, 0, static_cast<uint16_t>(self.frame_.text_size), static_cast<sdk::TextStyle>(style)};
    std::memcpy(self.frame_.text.data() + self.frame_.text_size, text, size);
    self.frame_.text_size += size;
    self.frame_.text[self.frame_.text_size++] = 0;
    return 0;
}

int Engine::Rectangle(lua_State* state, DrawKind kind) {
    auto& self = Self(state);
    if (self.phase_ != Phase::Render) return luaL_error(state, "drawing outside on_render");
    const auto x = Coordinate(state, 1, kWidth - 1), y = Coordinate(state, 2, kHeight - 1);
    const auto width = Coordinate(state, 3, kWidth - x), height = Coordinate(state, 4, kHeight - y);
    if (!width || !height) return 0;
    if (self.frame_.count == self.frame_.commands.size()) {
        self.error_ = Error::Drawing;
        return luaL_error(state, "drawing command limit");
    }
    self.frame_.commands[self.frame_.count++] = {kind, 1, static_cast<uint16_t>(x), static_cast<uint16_t>(y),
        static_cast<uint16_t>(width), static_cast<uint16_t>(height), 0};
    return 0;
}
int Engine::Rect(lua_State* state) { return Rectangle(state, DrawKind::Rect); }
int Engine::Fill(lua_State* state) { return Rectangle(state, DrawKind::Fill); }
int Engine::Exit(lua_State* state) {
    auto& self = Self(state);
    if (self.phase_ != Phase::Event && self.phase_ != Phase::Initialize)
        return luaL_error(state, "exit outside event callback");
    self.exit_ = true;
    return 0;
}

}  // namespace zectrix::runtime
