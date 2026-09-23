#include "library/TextStorage.hpp"

#include <format>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <system_error>

#include "library/Library.hpp"

namespace libms {
namespace {

constexpr int         kFormatVersion = 1;
constexpr std::string_view kBooksSection  = "[BOOKS]";
constexpr std::string_view kLedgerSection = "[LEDGER]";

std::string_view trim(std::string_view text) {
    const auto notSpace = [](unsigned char c) { return std::isspace(c) == 0; };
    while (!text.empty() && !notSpace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    while (!text.empty() && !notSpace(static_cast<unsigned char>(text.back()))) {
        text.remove_suffix(1);
    }
    return text;
}

std::vector<std::string_view> split(std::string_view line, char delimiter) {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (true) {
        const std::size_t pos = line.find(delimiter, start);
        if (pos == std::string_view::npos) {
            parts.push_back(line.substr(start));
            break;
        }
        parts.push_back(line.substr(start, pos - start));
        start = pos + 1;
    }
    return parts;
}

[[noreturn]] void failAt(std::size_t lineNo, std::string_view reason) {
    throw std::runtime_error{std::format("数据文件第 {} 行解析失败：{}", lineNo, reason)};
}

int parseInt(std::string_view text, std::size_t lineNo, std::string_view field) {
    try {
        std::size_t consumed = 0;
        const int   value    = std::stoi(std::string{text}, &consumed);
        if (consumed != text.size()) {
            failAt(lineNo, std::string{"字段 "} + std::string{field} + " 不是合法整数");
        }
        return value;
    } catch (const std::invalid_argument&) {
        failAt(lineNo, std::string{"字段 "} + std::string{field} + " 不是合法整数");
    } catch (const std::out_of_range&) {
        failAt(lineNo, std::string{"字段 "} + std::string{field} + " 超出整数范围");
    }
}

Book parseBook(const std::vector<std::string_view>& f, std::size_t lineNo) {
    if (f.size() != 7) {
        failAt(lineNo, std::format("图书行需要 7 个字段，实际 {}", f.size()));
    }
    BookStatus status = BookStatus::Available;
    if (!parseBookStatus(f[4], status)) {
        failAt(lineNo, std::format("未知状态标识 '{}'", f[4]));
    }

    const std::string isbn{f[0]};
    const std::string borrower{f[5]};

    if (status == BookStatus::Borrowed) {
        if (borrower.empty()) {
            failAt(lineNo, "已借出图书缺少读者信息");
        }
        // 借出状态无法直接构造，先建为在架再补挂借阅信息。
        Book book{isbn, std::string{f[1]}, std::string{f[2]}, parseInt(f[3], lineNo, "year")};
        book.assignBorrower(borrower, TextStorage::parseDate(f[6]));
        return book;
    }

    return Book{isbn, std::string{f[1]}, std::string{f[2]}, parseInt(f[3], lineNo, "year"), status};
}

BorrowRecord parseLedgerEntry(const std::vector<std::string_view>& f, std::size_t lineNo) {
    if (f.size() != 6) {
        failAt(lineNo, std::format("流水行需要 6 个字段，实际 {}", f.size()));
    }
    if (f[5] != "0" && f[5] != "1") {
        failAt(lineNo, std::format("returned 字段应为 0 或 1，实际 '{}'", f[5]));
    }
    return BorrowRecord{
        .isbn       = std::string{f[0]},
        .borrower   = std::string{f[1]},
        .borrowDate = TextStorage::parseDate(f[2]),
        .dueDate    = TextStorage::parseDate(f[3]),
        .returnDate = TextStorage::parseDate(f[4]),
        .returned   = f[5] == "1",
    };
}

}  // namespace

TextStorage::TextStorage(std::filesystem::path file) : file_{std::move(file)} {}

std::string TextStorage::formatDate(const Date& date) {
    if (!date.ok()) {
        return {};
    }
    return std::format("{:%Y-%m-%d}", date);
}

Date TextStorage::parseDate(std::string_view text) {
    if (text.empty()) {
        return Date{};
    }
    Date parsed{};
    std::istringstream stream{std::string{text}};
    if (!(stream >> std::chrono::parse("%Y-%m-%d", parsed))) {
        throw std::runtime_error{std::format("日期格式应为 YYYY-MM-DD，实际 '{}'", text)};
    }
    return parsed;
}

LibrarySnapshot TextStorage::load() const {
    LibrarySnapshot snapshot;

    std::ifstream input{file_};
    if (!input) {
        return snapshot;  // 首次运行：文件还不存在，按空库处理
    }

    enum class Section { Unknown, Books, Ledger };
    Section     section = Section::Unknown;
    std::size_t lineNo  = 0;
    std::string rawLine;

    while (std::getline(input, rawLine)) {
        ++lineNo;
        std::string_view line = trim(rawLine);
        if (line.empty() || line.starts_with('#')) {
            continue;
        }
        if (line == kBooksSection) {
            section = Section::Books;
            continue;
        }
        if (line == kLedgerSection) {
            section = Section::Ledger;
            continue;
        }
        if (line.starts_with("VERSION|")) {
            const int version = parseInt(line.substr(8), lineNo, "version");
            if (version != kFormatVersion) {
                failAt(lineNo, std::format("不支持的数据格式版本 {}", version));
            }
            continue;
        }

        const auto fields = split(line, '|');
        switch (section) {
            case Section::Books:  snapshot.books.push_back(parseBook(fields, lineNo)); break;
            case Section::Ledger: snapshot.ledger.push_back(parseLedgerEntry(fields, lineNo)); break;
            case Section::Unknown:
                failAt(lineNo, "出现分节标记之前的数据行");
        }
    }

    if (input.bad()) {
        throw std::runtime_error{std::format("读取 {} 失败", file_.string())};
    }
    return snapshot;
}

void TextStorage::save(const std::vector<Book*>& books, const std::list<BorrowRecord>& ledger) const {
    if (const auto parent = file_.parent_path(); !parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);  // 已存在不算错误
    }

    std::filesystem::path temp = file_;
    temp += ".tmp";

    {
        std::ofstream output{temp, std::ios::trunc};
        if (!output) {
            throw std::runtime_error{std::format("无法写入 {}", temp.string())};
        }

        output << "# Library Management System —— 数据文件，请勿手工改动分隔符\n";
        output << "# 本文件由程序自动生成，格式：| 分隔的纯文本\n";
        output << "VERSION|" << kFormatVersion << "\n\n";

        output << kBooksSection << "\n";
        output << "# isbn|title|author|publishYear|status|borrower|dueDate\n";
        for (const Book* book : books) {
            output << book->isbn() << '|' << book->title() << '|' << book->author() << '|'
                   << book->publishYear() << '|' << toStorageTag(book->status()) << '|'
                   << book->borrower() << '|' << formatDate(book->dueDate()) << '\n';
        }

        output << '\n' << kLedgerSection << "\n";
        output << "# isbn|borrower|borrowDate|dueDate|returnDate|returned\n";
        for (const BorrowRecord& record : ledger) {
            output << record.isbn << '|' << record.borrower << '|' << formatDate(record.borrowDate)
                   << '|' << formatDate(record.dueDate) << '|' << formatDate(record.returnDate) << '|'
                   << (record.returned ? '1' : '0') << '\n';
        }

        output.flush();
        if (!output) {
            throw std::runtime_error{std::format("写入 {} 时发生错误", temp.string())};
        }
    }

    std::error_code ec;
    std::filesystem::rename(temp, file_, ec);  // 原子替换，原文件要么完整保留要么被完整覆盖
    if (ec) {
        std::filesystem::remove(temp, ec);
        throw std::runtime_error{
            std::format("替换数据文件 {} 失败：{}", file_.string(), ec.message())};
    }
}

}  // namespace libms
