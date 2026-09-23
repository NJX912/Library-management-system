#include "library/ConsoleUI.hpp"
#include "library/Library.hpp"
#include "library/TextStorage.hpp"

#include <filesystem>
#include <format>
#include <iostream>
#include <string>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

/// Windows 控制台默认不是 UTF-8，中文会显示成乱码。
void enableUtf8Console() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
}

std::filesystem::path resolveDataFile(int argc, char** argv) {
    if (argc > 1) {
        return std::filesystem::path{argv[1]};
    }
    return std::filesystem::path{"data"} / "books.txt";
}

}  // namespace

int main(int argc, char** argv) {
    enableUtf8Console();

    const std::filesystem::path dataFile = resolveDataFile(argc, argv);
    libms::TextStorage storage{dataFile};
    libms::Library     library;

    // 启动即加载；文件不存在按空库处理，格式损坏则报错退出，绝不覆盖原文件。
    try {
        libms::LibrarySnapshot snapshot = storage.load();
        for (auto& book : snapshot.books) {
            library.adopt(std::move(book));
        }
        library.restoreLedger(std::move(snapshot.ledger));
        std::cout << std::format("已从 {} 载入 {} 种图书。\n",
                                 dataFile.string(),
                                 library.bookCount());
    } catch (const std::exception& error) {
        std::cerr << "载入数据失败：" << error.what() << '\n'
                  << "为避免覆盖现有数据，程序将退出。\n";
        return 1;
    }

    libms::ConsoleUI ui{library, storage};
    return ui.run();
}
