#include "zectrix_app_storage.h"
#include "zectrix_micro_app_controller.h"
#include "zectrix_micro_app_view.h"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <unistd.h>

using namespace zectrix;
using app::MicroAppScene;
using app::MicroAppDecision;
using storage::BookWriteResult;
namespace fs = std::filesystem;
static_assert(storage::AppStorage::kSourceLimit == runtime::kSourceLimit);

static std::string Read(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    assert(stream.good());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

static void Install(storage::BookStorage& storage, const char* name, const std::string& source) {
    assert(storage.BeginManagement() == ESP_OK);
    storage::BookUpload upload;
    assert(storage::AppStorage(storage).BeginUpload(name, source.size(), &upload) == BookWriteResult::Ok);
    assert(upload.Write(source.data(), source.size()) == BookWriteResult::Ok);
    assert(upload.Commit() == BookWriteResult::Ok);
    assert(storage.EndManagement() == ESP_OK);
}

class Library final : public app::MicroAppLibrary {
public:
    explicit Library(storage::BookStorage& storage) : apps(storage) {}
    esp_err_t List(storage::BookEntry* entries, std::size_t capacity, std::size_t* count,
                   bool* more, const char* cursor, bool previous) override {
        return fail_list ? ESP_FAIL : apps.List(entries, capacity, count, more, cursor, previous);
    }
    esp_err_t Open(const char* name, storage::BookFile* file) override {
        ++opens;
        return apps.Open(name, file);
    }
    storage::AppStorage apps;
    unsigned opens = 0;
    bool fail_list = false;
};

static MicroAppDecision Click(app::MicroAppController& controller, sdk::Button button = sdk::Button::Ok) {
    return controller.Handle({button, sdk::InputAction::Click});
}
static MicroAppDecision Back(app::MicroAppController& controller) {
    return controller.Handle({sdk::Button::Ok, sdk::InputAction::LongPress});
}
static void Loaded(app::MicroAppController& controller) {
    unsigned ticks = 0;
    while (controller.busy()) { controller.Tick(); assert(++ticks <= 32); }
}
static bool HasText(const runtime::Frame& frame, const char* wanted) {
    for (std::size_t i = 0; i < frame.count; ++i)
        if (frame.commands[i].kind == runtime::DrawKind::Text &&
            std::strcmp(frame.text.data() + frame.commands[i].text, wanted) == 0) return true;
    return false;
}

static void Preview(const app::MicroAppController& controller, const fs::path& destination) {
    ZectrixCanvas canvas;
    canvas.Clear();
    canvas.FillRect(0, 24, 400, 20, true);
    canvas.Text(10, 26, controller.name(), 1, true);
    canvas.Text(8, 277, "Hold OK Back   Hold DOWN Off");
    canvas.SetClip({0, 24, 400, 276});
    const auto before = canvas;
    ui::DrawMicroAppFrame(canvas, controller.engine().frame());
    assert(canvas.clip().y == 24);
    for (int y = 0; y < 300; ++y) for (int x = 0; x < 400; ++x) {
        if (x >= 12 && x < 388 && y >= 66 && y < 258) continue;
        const auto byte = y * 50 + x / 8;
        assert(((canvas.data()[byte] ^ before.data()[byte]) & (0x80 >> (x % 8))) == 0);
    }
    std::ofstream output(destination, std::ios::binary);
    output << "P4\n400 300\n";
    for (std::size_t i = 0; i < canvas.size(); ++i) output.put(static_cast<char>(~canvas.data()[i]));
    assert(output.good());
}

static void Storage(const fs::path& root) {
    storage::BookStorage books(root.c_str());
    storage::AppStorage apps(books);
    assert(storage::BookStorage::ValidName("notes.txt") && !storage::BookStorage::ValidName("App.lua"));
    assert(apps.ValidName("计算器.lua") && apps.ValidName("App.LUA"));
    for (const char* invalid : {"../app.lua", "folder/app.lua", "folder\\app.lua", ".lua", "book.txt", "bad\n.lua", "\xff.lua"})
        assert(!apps.ValidName(invalid));
    assert(!apps.ValidName((std::string(44, 'a') + ".lua").c_str()));
    std::ofstream(root / "book.txt") << "keep this book";
    std::ofstream(root / "loose.lua") << "while true do end";
    Install(books, "One.lua", "while true do end");
    storage::BookEntry entries[5];
    std::size_t count = 0;
    bool more = false;
    assert(books.List(entries, 5, &count, &more) == ESP_OK && count == 1);
    assert(std::string(entries[0].name.data()) == "book.txt");
    assert(apps.List(entries, 5, &count, &more) == ESP_OK && count == 1);
    assert(std::string(entries[0].name.data()) == "One.lua");
    storage::BookFile file;
    assert(apps.Open("One.lua", &file) == ESP_OK);
    assert(books.BeginManagement() == ESP_ERR_INVALID_STATE);
    file.Close();
    assert(books.BeginManagement() == ESP_OK);
    assert(apps.Open("One.lua", &file) == ESP_ERR_INVALID_STATE);
    storage::BookUpload upload;
    assert(apps.BeginUpload("One.lua", 5, &upload) == BookWriteResult::Exists);
    assert(apps.BeginUpload("Big.lua", 32769, &upload) == BookWriteResult::Invalid);
    assert(apps.BeginUpload("Empty.lua", 0, &upload) == BookWriteResult::Invalid);
    assert(apps.BeginUpload("Partial.lua", 5, &upload) == BookWriteResult::Ok);
    assert(upload.Write("no", 2) == BookWriteResult::Ok);
    assert(upload.Commit() == BookWriteResult::Invalid);
    assert(apps.Remove("One.lua") == BookWriteResult::Busy);
    upload.Abort();
    assert(!fs::exists(root / ".upload.part") && !fs::exists(root / ".app-Partial.lua"));
    assert(apps.Remove("book.txt") == BookWriteResult::Invalid);
    assert(apps.Remove("One.lua") == BookWriteResult::Ok);
    assert(apps.Remove("One.lua") == BookWriteResult::NotFound);
    assert(Read(root / "book.txt") == "keep this book");
    assert(books.EndManagement() == ESP_OK);
    std::ofstream(root / ".app-Oversize.lua") << std::string(32769, ' ');
    assert(apps.Open("Oversize.lua", &file) == ESP_ERR_INVALID_SIZE);
    fs::create_symlink(root / "book.txt", root / ".app-Link.lua");
    assert(apps.Open("Link.lua", &file) == ESP_ERR_INVALID_SIZE);
    fs::remove(root / ".app-Oversize.lua");
    fs::remove(root / ".app-Link.lua");
}

static void Scenes(const fs::path& root, const fs::path& pilots, const fs::path& previews) {
    storage::BookStorage books(root.c_str());
    Install(books, "Calculator.lua", Read(pilots / "Calculator.lua"));
    Install(books, "Flashcards.lua", Read(pilots / "Flashcards.lua"));
    Library library(books);
    app::MicroAppController controller(library);
    assert(sdk::IsOk(controller.Start()));
    assert(controller.count() == 2 && library.opens == 0 && controller.engine().heap().live == 0);
    Click(controller);
    assert(controller.busy() && books.BeginManagement() == ESP_ERR_INVALID_STATE);
    Loaded(controller);
    assert(controller.scene() == MicroAppScene::Running);
    Click(controller, sdk::Button::Up);
    for (int i = 0; i < 6; ++i) Click(controller);
    Click(controller, sdk::Button::Up); Click(controller, sdk::Button::Up);
    Click(controller); Click(controller);
    assert(HasText(controller.engine().frame(), "Result: 102"));
    Preview(controller, previews / "calculator.pbm");
    const auto frame = controller.engine().frame();
    controller.Presented(false);
    assert(controller.Tick() == MicroAppDecision::RenderQuality);
    assert(std::memcmp(&frame, &controller.engine().frame(), sizeof(frame)) == 0);
    controller.Presented(true);
    assert(controller.Tick() == MicroAppDecision::None);
    assert(controller.Handle({sdk::Button::Up, sdk::InputAction::LongPress}) == MicroAppDecision::None);
    Back(controller);
    assert(controller.scene() == MicroAppScene::List && controller.engine().heap().live == 0);
    Click(controller, sdk::Button::Down);
    Click(controller); Loaded(controller); Click(controller);
    assert(HasText(controller.engine().frame(), "Electrophoretic display"));
    Preview(controller, previews / "flashcards.pbm");
    assert(controller.Handle({sdk::Button::Down, sdk::InputAction::LongPress}) == MicroAppDecision::Shutdown);
    controller.Stop(); controller.Stop();
    assert(controller.engine().heap().live == 0);
    assert(books.BeginManagement() == ESP_OK && books.EndManagement() == ESP_OK);

    for (int i = 0; i < 100; ++i) {
        assert(sdk::IsOk(controller.Start()));
        Click(controller); Loaded(controller);
        assert(controller.scene() == MicroAppScene::Running);
        Back(controller);
        assert(controller.engine().heap().live == 0 && controller.selected() == 0);
        assert(Back(controller) == MicroAppDecision::Back);
        controller.Stop();
    }

    Install(books, "A-Slow.lua", std::string(16000, ' ') + "while true do end");
    assert(sdk::IsOk(controller.Start()));
    Click(controller); controller.Tick();
    assert(controller.busy());
    Back(controller);
    assert(controller.scene() == MicroAppScene::List && controller.engine().heap().live == 0);
    assert(books.BeginManagement() == ESP_OK && books.EndManagement() == ESP_OK);
    Click(controller); Loaded(controller);
    assert(controller.scene() == MicroAppScene::Error && controller.engine().error() == runtime::Error::Instructions);
    assert(controller.engine().heap().live == 0);
    Click(controller);
    assert(controller.scene() == MicroAppScene::List);
    controller.Stop();

    library.fail_list = true;
    assert(sdk::IsOk(controller.Start()) && controller.count() == 0 && controller.storage_result() != ESP_OK);
    library.fail_list = false;
    Click(controller);
    assert(controller.count() == 3);
    controller.Stop();

    for (unsigned i = 0; i < 13; ++i) {
        const auto name = "Card-" + std::to_string(i) + ".lua";
        Install(books, name.c_str(), "function on_render() end; function on_event() end");
    }
    assert(sdk::IsOk(controller.Start()));
    const auto first = controller.entry(0);
    for (int i = 0; i < 3; ++i) {
        Click(controller, sdk::Button::Up);
        Click(controller);
        assert(controller.page() == static_cast<unsigned>(i + 1));
    }
    assert(controller.count() == 1 && controller.rows() == 2);
    for (int i = 0; i < 3; ++i) {
        for (std::size_t row = 0; row < controller.count(); ++row) Click(controller, sdk::Button::Down);
        Click(controller);
    }
    assert(controller.page() == 0 && std::strcmp(first.name.data(), controller.entry(0).name.data()) == 0);
    controller.Stop();
}

static void Faults(const fs::path& root, const fs::path& previews) {
    fs::create_directory(root);
    storage::BookStorage books(root.c_str());
    Library library(books);
    app::MicroAppController controller(library);
    auto replace = [&](const std::string& source) {
        controller.Stop();
        assert(books.BeginManagement() == ESP_OK);
        const auto removed = library.apps.Remove("App.lua");
        assert(removed == BookWriteResult::Ok || removed == BookWriteResult::NotFound);
        assert(books.EndManagement() == ESP_OK);
        Install(books, "App.lua", source);
        assert(sdk::IsOk(controller.Start()));
        Click(controller);
    };

    replace("local n = 0; function on_event(k) return k == note4.UP end; "
            "function on_render() n = n + 1; note4.text(0,0,n) end");
    Loaded(controller);
    assert(HasText(controller.engine().frame(), "1"));
    controller.Presented(false);
    for (int i = 0; i < 8; ++i) assert(controller.Tick() == MicroAppDecision::RenderQuality);
    assert(HasText(controller.engine().frame(), "1"));
    controller.Presented(true);
    assert(Click(controller, sdk::Button::Down) == MicroAppDecision::None);
    assert(HasText(controller.engine().frame(), "1"));
    Click(controller, sdk::Button::Up);
    assert(HasText(controller.engine().frame(), "2"));

    replace("function on_event() while true do end end; function on_render() "
            "note4.fill(0,0,note4.width,note4.height); note4.text(375,191,'edge',2) end");
    Loaded(controller);
    Preview(controller, previews / "clipped.pbm");
    Click(controller);
    assert(controller.scene() == MicroAppScene::Error && controller.engine().error() == runtime::Error::Instructions);
    assert(controller.engine().heap().live == 0);
    Back(controller);
    assert(controller.scene() == MicroAppScene::List);

    replace("note4.exit()");
    Loaded(controller);
    assert(controller.scene() == MicroAppScene::List && controller.engine().heap().live == 0);
    replace("function on_event() note4.exit(); return true end; function on_render() end");
    Loaded(controller); Click(controller);
    assert(controller.scene() == MicroAppScene::List && controller.engine().heap().live == 0);

    replace(std::string(12000, ' ') + "function on_render() end");
    std::ofstream(root / ".app-App.lua", std::ios::trunc) << "short";
    Loaded(controller);
    assert(controller.scene() == MicroAppScene::Error && controller.storage_result() != ESP_OK);
    assert(books.BeginManagement() == ESP_OK && books.EndManagement() == ESP_OK);
    Back(controller);
    replace(std::string(12000, ' ') + "function on_render() end");
    assert(controller.Handle({sdk::Button::Down, sdk::InputAction::LongPress}) == MicroAppDecision::Shutdown);
    controller.Stop();
    assert(!controller.busy() && controller.engine().heap().live == 0);
    assert(books.BeginManagement() == ESP_OK && books.EndManagement() == ESP_OK);
}

int main(int argc, char** argv) {
    assert(argc == 3);
    char pattern[] = "/tmp/note4-micro-apps-XXXXXX";
    const auto* temporary = mkdtemp(pattern);
    assert(temporary);
    const fs::path root(temporary);
    Storage(root);
    Scenes(root, argv[1], argv[2]);
    Faults(root / "faults", argv[2]);
    fs::remove_all(root);
    std::cout << "Micro-apps: storage isolation, paging, pilots, exit, recovery, clipped views and 100 lifetimes passed.\n";
}
