#include "library/ConsoleUI.hpp"

#include <algorithm>
#include <cstddef>
#include <format>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace libms {
namespace {

constexpr int kDefaultLoanDays = 30;

/// UTF-8 码点数与终端显示宽度（CJK 视作 2 列），用于对齐表格。
std::size_t displayWidth(std::string_view text) {
    std::size_t width = 0;
    for (std::size_t i = 0; i < text.size();) {
        const auto byte = static_cast<unsigned char>(text[i]);
        std::size_t length = 1;
        if ((byte & 0x80U) == 0x00U) {
            length = 1;
        } else if ((byte & 0xE0U) == 0xC0U) {
            length = 2;
        } else if ((byte & 0xF0U) == 0xE0U) {
            length = 3;
        } else if ((byte & 0xF8U) == 0xF0U) {
            length = 4;
        }
        width += (length == 1) ? 1U : 2U;
        i += std::min(length, text.size() - i);
    }
    return width;
}

std::string padRight(std::string_view text, std::size_t width) {
    const std::size_t actual = displayWidth(text);
    return std::format("{}{}", text, std::string(actual < width ? width - actual : 1, ' '));
}

std::string_view trimmed(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return text;
}

/// 读取一行；输入流结束（Ctrl+Z / Ctrl+D）返回 nullopt。
std::optional<std::string> readLine(std::string_view label) {
    std::cout << label;
    std::cout.flush();
    std::string line;
    if (!std::getline(std::cin, line)) {
        return std::nullopt;
    }
    return std::string{trimmed(line)};
}

bool reportError(const std::exception& error) {
    std::cout << "  [!] 操作失败：" << error.what() << '\n';
    return false;
}

}  // namespace

ConsoleUI::ConsoleUI(Library& library, TextStorage& storage)
    : library_{library}, storage_{storage} {}

void ConsoleUI::printMenu() const {
    std::cout << "\n================ 图书管理系统 ================\n"
                 "   1) 列出全部图书          2) 添加图书\n"
                 "   3) 修改图书信息          4) 删除图书\n"
                 "   5) 借书                  6) 还书\n"
                 "   7) 标记图书状态          8) 搜索图书\n"
                 "   9) 查看借阅流水         10) 保存到文件\n"
                 "   0) 保存并退出\n"
                 "==============================================\n";
}

void ConsoleUI::printSummary() const {
    std::cout << std::format("\n数据文件：{}\n", storage_.path().string());
    std::cout << std::format("馆藏 {} 种，其中借出 {} 种，历史流水 {} 条。\n",
                             library_.bookCount(),
                             library_.borrowedCount(),
                             library_.ledger().size());
}

void ConsoleUI::printBooks(const std::vector<const Book*>& books) const {
    if (books.empty()) {
        std::cout << "  （没有匹配的图书）\n";
        return;
    }
    std::cout << "  " << padRight("ISBN", 20) << padRight("书名", 28) << padRight("作者", 20)
              << padRight("年份", 6) << padRight("状态", 10) << "读者 / 应还日期\n";
    std::cout << "  " << std::string(96, '-') << '\n';

    for (const Book* book : books) {
        std::string holding;
        if (book->isBorrowed()) {
            holding = std::format("{} / {}", book->borrower(), TextStorage::formatDate(book->dueDate()));
        }
        std::cout << "  " << padRight(book->isbn(), 20) << padRight(book->title(), 28)
                  << padRight(book->author(), 20) << padRight(std::to_string(book->publishYear()), 6)
                  << padRight(toDisplayName(book->status()), 10) << holding << '\n';
    }
}

void ConsoleUI::handleAddBook() {
    const auto isbn = readLine("  ISBN：");
    if (!isbn) return;
    const auto title = readLine("  书名：");
    if (!title) return;
    const auto author = readLine("  作者：");
    if (!author) return;
    const auto yearText = readLine("  出版年份：");
    if (!yearText) return;

    try {
        const int year = std::stoi(*yearText);
        const Book& book = library_.addBook(*isbn, *title, *author, year);
        std::cout << "  [√] 已入库：" << book.title() << '\n';
    } catch (const std::exception& error) {
        reportError(error);
    }
}

void ConsoleUI::handleUpdateBook() {
    const auto isbn = readLine("  要修改的 ISBN：");
    if (!isbn) return;
    const Book* existing = library_.tryFind(*isbn);
    if (existing == nullptr) {
        std::cout << "  [!] 找不到该 ISBN。\n";
        return;
    }

    std::cout << "  直接回车表示保持原值。\n";
    const auto title = readLine(std::format("  书名[{}]：", existing->title()));
    if (!title) return;
    const auto author = readLine(std::format("  作者[{}]：", existing->author()));
    if (!author) return;
    const auto yearText = readLine(std::format("  出版年份[{}]：", existing->publishYear()));
    if (!yearText) return;

    try {
        const int year = yearText->empty() ? existing->publishYear() : std::stoi(*yearText);
        library_.updateBook(*isbn,
                            title->empty() ? existing->title() : *title,
                            author->empty() ? existing->author() : *author,
                            year);
        std::cout << "  [√] 已更新。\n";
    } catch (const std::exception& error) {
        reportError(error);
    }
}

void ConsoleUI::handleRemoveBook() {
    const auto isbn = readLine("  要删除的 ISBN：");
    if (!isbn) return;
    try {
        const std::string title = library_.find(*isbn).title();
        library_.removeBook(*isbn);
        std::cout << "  [√] 已删除《" << title << "》。\n";
    } catch (const std::exception& error) {
        reportError(error);
    }
}

void ConsoleUI::handleSearch() const {
    const auto keyword = readLine("  关键字（书名/作者/ISBN）：");
    if (!keyword) return;
    printBooks(library_.searchByKeyword(*keyword));
}

void ConsoleUI::handleBorrow() {
    const auto isbn = readLine("  要借阅的 ISBN：");
    if (!isbn) return;
    const auto borrower = readLine("  读者姓名：");
    if (!borrower) return;
    const auto daysText = readLine(std::format("  借期天数[{}]：", kDefaultLoanDays));
    if (!daysText) return;

    try {
        const int days = daysText->empty() ? kDefaultLoanDays : std::stoi(*daysText);
        const BorrowRecord& record = library_.borrow(*isbn, *borrower, days);
        std::cout << std::format("  [√] 《{}》已借给 {}，应还 {}。\n",
                                 library_.find(*isbn).title(),
                                 record.borrower,
                                 TextStorage::formatDate(record.dueDate));
    } catch (const std::exception& error) {
        reportError(error);
    }
}

void ConsoleUI::handleGiveBack() {
    const auto isbn = readLine("  要归还的 ISBN：");
    if (!isbn) return;
    try {
        const BorrowRecord record = library_.giveBack(*isbn);
        std::cout << std::format("  [√] 《{}》已由 {} 于 {} 归还。\n",
                                 library_.find(*isbn).title(),
                                 record.borrower,
                                 TextStorage::formatDate(record.returnDate));
    } catch (const std::exception& error) {
        reportError(error);
    }
}

void ConsoleUI::handleMarkStatus() {
    const auto isbn = readLine("  ISBN：");
    if (!isbn) return;
    std::cout << "  目标状态：1) 在架  2) 遗失  3) 下架\n";
    const auto choice = readLine("  请选择：");
    if (!choice) return;

    std::optional<BookStatus> target;
    if (*choice == "1") {
        target = BookStatus::Available;
    } else if (*choice == "2") {
        target = BookStatus::Lost;
    } else if (*choice == "3") {
        target = BookStatus::OffShelf;
    } else {
        std::cout << "  [!] 无效选项。\n";
        return;
    }

    try {
        library_.markStatus(*isbn, *target);
        std::cout << std::format("  [√] 状态已标记为「{}」。\n", toDisplayName(*target));
    } catch (const std::exception& error) {
        reportError(error);
    }
}

void ConsoleUI::handleShowLedger() const {
    const auto& ledger = library_.ledger();
    if (ledger.empty()) {
        std::cout << "  （暂无借阅记录）\n";
        return;
    }
    std::cout << "  " << padRight("ISBN", 20) << padRight("读者", 12) << padRight("借出", 12)
              << padRight("应还", 12) << padRight("归还", 12) << "状态\n";
    std::cout << "  " << std::string(80, '-') << '\n';
    for (const BorrowRecord& record : ledger) {
        std::cout << "  " << padRight(record.isbn, 20) << padRight(record.borrower, 12)
                  << padRight(TextStorage::formatDate(record.borrowDate), 12)
                  << padRight(TextStorage::formatDate(record.dueDate), 12)
                  << padRight(TextStorage::formatDate(record.returnDate), 12)
                  << (record.returned ? "已归还" : "在借") << '\n';
    }
}

void ConsoleUI::handleSave() const {
    try {
        storage_.save(library_.books(), library_.ledger());
        std::cout << "  [√] 已保存到 " << storage_.path().string() << '\n';
    } catch (const std::exception& error) {
        reportError(error);
    }
}

int ConsoleUI::run() {
    printSummary();

    while (true) {
        printMenu();
        const auto choice = readLine("请选择操作：");
        if (!choice) {
            std::cout << "\n输入已结束，保存后退出。\n";
            handleSave();
            return 0;
        }

        if (*choice == "0") {
            handleSave();
            std::cout << "再见。\n";
            return 0;
        }
        if (*choice == "1") {
            printBooks(library_.searchByKeyword(""));  // 空关键字即「全部」
        } else if (*choice == "2") {
            handleAddBook();
        } else if (*choice == "3") {
            handleUpdateBook();
        } else if (*choice == "4") {
            handleRemoveBook();
        } else if (*choice == "5") {
            handleBorrow();
        } else if (*choice == "6") {
            handleGiveBack();
        } else if (*choice == "7") {
            handleMarkStatus();
        } else if (*choice == "8") {
            handleSearch();
        } else if (*choice == "9") {
            handleShowLedger();
        } else if (*choice == "10") {
            handleSave();
        } else {
            std::cout << "  [!] 无效选项，请重新输入。\n";
        }
    }
}

}  // namespace libms
