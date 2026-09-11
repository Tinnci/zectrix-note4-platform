#include "zectrix_runtime.h"

#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

using namespace zectrix::runtime;

static bool Start(Engine& engine, const std::string& source) {
    return engine.Start(reinterpret_cast<const uint8_t*>(source.data()), source.size());
}
static std::string Read(const std::string& path) {
    std::ifstream file(path);
    assert(file.good());
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
static bool HasText(const Frame& frame, const char* wanted) {
    for (std::size_t i = 0; i < frame.count; ++i) {
        if (frame.commands[i].kind == DrawKind::Text &&
            std::strcmp(frame.text.data() + frame.commands[i].text, wanted) == 0) return true;
    }
    return false;
}

int main(int argc, char** argv) {
    assert(argc == 2);
    Engine engine;
    assert(Start(engine, Read(std::string(argv[1]) + "/Calculator.lua")));
    assert(engine.Draw());
    // Enter 100 + 002 and execute it using the real pilot event functions.
    assert(engine.Handle(Key::Up));
    for (int i = 0; i < 6; ++i) assert(engine.Handle(Key::Ok));
    assert(engine.Handle(Key::Up) && engine.Handle(Key::Up));
    assert(engine.Handle(Key::Ok) && engine.Handle(Key::Ok));
    assert(engine.Draw() && HasText(engine.frame(), "Result: 102"));
    engine.Stop();
    assert(engine.heap().live == 0);

    assert(Start(engine, "function on_render() for s=0,31 do note4.text(0,0,'styled',1,s) end "
                         "note4.text(0,0,'default'); note4.rect(0,0,2,2) end"));
    assert(engine.Draw() && engine.frame().count == 34);
    for (unsigned flags = 0; flags < 32; ++flags)
        assert(static_cast<unsigned>(engine.frame().commands[flags].style) == flags);
    assert(engine.frame().commands[32].style == zectrix::sdk::TextStyle::Regular);
    assert(engine.frame().commands[33].style == zectrix::sdk::TextStyle::Regular);
    const auto copied = engine.frame();
    engine.Stop();
    assert(copied.commands[3].style == (zectrix::sdk::TextStyle::Bold | zectrix::sdk::TextStyle::Italic));
    for (const char* flags : {"-1", "32", "256", "1099511627776"}) {
        assert(Start(engine, std::string("function on_render() note4.text(0,0,'bad',1,") + flags + ") end"));
        assert(!engine.Draw() && engine.error() == Error::Drawing && engine.heap().live == 0);
    }

    const auto cards = Read(std::string(argv[1]) + "/Flashcards.lua");
    for (int i = 0; i < 100; ++i) {
        assert(Start(engine, cards) && engine.Draw());
        assert(HasText(engine.frame(), "OK: reveal answer"));
        assert(engine.Handle(Key::Ok) && engine.Draw());
        assert(HasText(engine.frame(), "Electrophoretic display"));
        assert(engine.Handle(Key::Down) && engine.Draw());
        assert(HasText(engine.frame(), "CARD 2 / 6"));
        engine.Stop();
        assert(engine.heap().live == 0 && engine.heap().peak <= kHeapLimit);
    }

    assert(!Start(engine, "while true do end"));
    assert(engine.error() == Error::Instructions && engine.heap().live == 0);
    assert(Start(engine, "function on_event(k) while true do end end"));
    assert(!engine.Handle(Key::Ok));
    assert(engine.error() == Error::Instructions && engine.heap().live == 0);
    assert(Start(engine, "function on_render() while true do end end"));
    assert(!engine.Draw() && engine.error() == Error::Instructions && engine.heap().live == 0);

    assert(!Start(engine, "local t = {}; local s = 'x'; for i = 1,32 do s = s .. s; t[i] = s end"));
    assert(engine.error() == Error::Memory && engine.heap().live == 0);
    Engine tiny(512);
    assert(!Start(tiny, cards) && tiny.heap().live == 0 && tiny.error() == Error::Memory);
    for (std::size_t limit = 256; limit < 20000; limit += 256) {
        Engine limited(limit);
        if (Start(limited, cards)) limited.Draw();
        limited.Stop();
        assert(limited.heap().live == 0 && limited.heap().peak <= limit);
    }
    assert(!Start(engine, "local value = " + std::string(200, '(') + "1" + std::string(200, ')')));
    assert(engine.heap().live == 0);
    std::string nested;
    for (unsigned i = 0; i < 64; ++i) nested += "function f() ";
    for (unsigned i = 0; i < 64; ++i) nested += "end ";
    assert(!Start(engine, nested) && engine.heap().live == 0);
    assert(!Start(engine, "function ("));
    assert(engine.error() == Error::InvalidSource && engine.heap().live == 0);
    assert(!Start(engine, std::string("\x1bLua", 4)));
    assert(!Start(engine, std::string(kSourceLimit + 1, ' ')));
    assert(Start(engine, "function on_render() for i = 1,65 do note4.rect(0,0,1,1) end end"));
    assert(!engine.Draw() && engine.error() == Error::Drawing && engine.heap().live == 0);
    assert(Start(engine, "function on_render() note4.text(0,0,'\\255') end"));
    assert(!engine.Draw() && engine.heap().live == 0);
    assert(Start(engine, "function on_render() note4.rect(375,0,2,1) end"));
    assert(!engine.Draw() && engine.heap().live == 0);
    assert(Start(engine, "function on_render() note4.rect(100,100,0,40); note4.fill(0,0,20,0) end"));
    assert(engine.Draw() && engine.frame().count == 0);
    assert(Start(engine, "function on_render() for i = 1,20 do note4.text(0,0,'" + std::string(128, 'x') + "') end end"));
    assert(!engine.Draw() && engine.error() == Error::Drawing && engine.heap().live == 0);
    assert(Start(engine, "function on_event() note4.text(0,0,'wrong phase') end"));
    assert(!engine.Handle(Key::Ok) && engine.heap().live == 0);
    assert(Start(engine, "function on_event() note4.exit() end"));
    assert(engine.Handle(Key::Ok) && engine.exit_requested());
    engine.Stop();
    assert(engine.heap().live == 0 && !engine.exit_requested());
    assert(Start(engine, "function on_render() if io or os or package or debug or coroutine or load or setmetatable then note4.text(0,0,'unsafe') else note4.text(0,0,'restricted') end end"));
    assert(engine.Draw() && HasText(engine.frame(), "restricted"));
    engine.Stop();
    std::cout << "Runtime: pilots, quota, malformed code, draw bounds and 100 lifetimes passed.\n";
}
