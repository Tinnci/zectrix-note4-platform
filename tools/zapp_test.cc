#include "zectrix_app_storage.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

using namespace zectrix;
namespace fs = std::filesystem;

int main(int argc, char** argv) {
    assert(argc == 3);
    fs::create_directories(argv[2]);
    storage::BookStorage books(argv[2]);
    storage::AppStorage apps(books);
    assert(apps.ValidName("计算器.ZAPP") && !books.ValidName("Calculator.zapp"));
    assert(!apps.ValidName(".zapp") && !apps.ValidName("../Bad.zapp"));
    assert(apps.ValidName((std::string(42, 'a') + ".zapp").c_str()));
    assert(!apps.ValidName((std::string(43, 'a') + ".zapp").c_str()));
    unsigned count = 0;
    for (const auto& entry : fs::directory_iterator(argv[1])) {
        if (entry.path().extension() != ".zapp") continue;
        std::ifstream input(entry.path(), std::ios::binary);
        const std::string data{std::istreambuf_iterator<char>(input), {}};
        const bool valid = entry.path().filename().string().rfind("valid-", 0) == 0;
        for (const unsigned chunk : {1, 7, 159, 160, 161, 192, 1020}) {
            package::Validator validator;
            validator.Reset(data.size());
            bool accepted = true;
            for (std::size_t offset = 0; offset < data.size() && accepted; offset += chunk)
                accepted = validator.Feed(data.data() + offset, std::min<std::size_t>(chunk, data.size() - offset));
            if ((accepted && validator.Complete()) != valid) {
                std::cerr << entry.path() << " chunk=" << chunk << '\n';
                return 1;
            }
            assert(!validator.Feed("x", 1));
            assert(!validator.Complete());
        }
        using Result = storage::BookWriteResult;
        assert(books.BeginManagement() == ESP_OK);
        storage::BookUpload upload;
        auto result = apps.BeginUpload("Fixture.zapp", data.size(), &upload);
        for (std::size_t offset = 0; offset < data.size() && result == Result::Ok; offset += 137)
            result = upload.Write(data.data() + offset, std::min<std::size_t>(137, data.size() - offset));
        if (result == Result::Ok) result = upload.Commit();
        assert((result == Result::Ok) == valid);
        if (!valid) assert(upload.Commit() != Result::Ok);
        upload.Abort();
        assert(!fs::exists(fs::path(argv[2]) / ".upload.part"));
        assert(fs::exists(fs::path(argv[2]) / ".app-Fixture.zapp") == valid);
        if (valid) {
            assert(apps.BeginUpload("Fixture.zapp", data.size(), &upload) == Result::Exists);
            storage::BookFile file;
            assert(apps.OpenManaged("Fixture.zapp", &file) == ESP_OK);
            assert(books.EndManagement() == ESP_ERR_INVALID_STATE);
            std::string copy(data.size(), '\0');
            assert(file.Read(0, copy.data(), copy.size()) && copy == data);
            file.Close();
            assert(apps.Remove("Fixture.zapp") == Result::Ok);
        }
        assert(books.EndManagement() == ESP_OK);
        ++count;
    }
    assert(count > 40);
    std::cout << "Packages: " << count << " independent fixtures, fragmented validation and transactional installation passed.\n";
}
