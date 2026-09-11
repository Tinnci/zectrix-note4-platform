#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

struct lua_State;
struct lua_Debug;

namespace zectrix::runtime {

inline constexpr std::size_t kSourceLimit = 32768;
inline constexpr std::size_t kHeapLimit = 128 * 1024;
inline constexpr int kInstructionLimit = 10000;
inline constexpr int kWidth = 376, kHeight = 192;

enum class Key : uint8_t { Up = 1, Down, Ok };
enum class Error : uint8_t { None, InvalidSource, Memory, Instructions, Guest, Drawing };
enum class DrawKind : uint8_t { Text, Rect, Fill };

struct DrawCommand {
    DrawKind kind = DrawKind::Text;
    uint8_t scale = 1;
    uint16_t x = 0, y = 0, width = 0, height = 0, text = 0;
};

// A completed frame owns every byte; rendering never borrows Lua memory.
struct Frame {
    std::array<DrawCommand, 64> commands{};
    std::array<char, 2048> text{};
    std::size_t count = 0, text_size = 0;
};

struct HeapStats { std::size_t live = 0, peak = 0, rejected = 0; };

// One foreground owner calls this object serially. No tasks or hardware calls.
class Engine final {
public:
    explicit Engine(std::size_t heap_limit = kHeapLimit) : heap_limit_(heap_limit) {}
    ~Engine() { Stop(); }
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    bool Start(const uint8_t* source, std::size_t size);
    bool Handle(Key key);
    bool Draw();
    void Stop();
    bool active() const { return owner_ != nullptr && error_ == Error::None; }
    bool exit_requested() const { return exit_; }
    bool redraw_requested() const { return redraw_; }
    const Frame& frame() const { return frame_; }
    Error error() const { return error_; }
    const char* detail() const { return detail_.data(); }
    HeapStats heap() const { return heap_; }

private:
    enum class Phase : uint8_t { Idle, Initialize, Event, Render };
    static Engine& Self(lua_State* state);
    static void* Allocate(void* context, void* pointer, std::size_t old_size, std::size_t size);
    static void Budget(lua_State* state, lua_Debug* debug);
    static int Prepare(lua_State* state);
    static int Invoke(lua_State* state);
    static int Finish(lua_State* state, int status, std::intptr_t context);
    static int Text(lua_State* state);
    static int Rect(lua_State* state);
    static int Fill(lua_State* state);
    static int Exit(lua_State* state);
    static int Rectangle(lua_State* state, DrawKind kind);
    static int Coordinate(lua_State* state, int index, int upper);
    bool Run(Phase phase);
    bool Result(int status);
    bool Fail(Error error, const char* detail);
    void Close();

    lua_State* owner_ = nullptr;
    lua_State* callback_ = nullptr;
    std::size_t heap_limit_;
    HeapStats heap_{};
    Frame frame_{};
    std::array<char, 128> detail_{};
    Error error_ = Error::None;
    Phase phase_ = Phase::Idle;
    Key key_ = Key::Up;
    bool exit_ = false, redraw_ = false;
};

}  // namespace zectrix::runtime
