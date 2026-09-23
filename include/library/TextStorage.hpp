#pragma once

#include "library/Book.hpp"

#include <filesystem>
#include <list>
#include <string>
#include <vector>

namespace libms {

/// 从磁盘读到的一份完整数据集。
struct LibrarySnapshot {
    std::vector<Book>       books;
    std::list<BorrowRecord> ledger;
};

/// 纯文本存储（| 分隔，分节）。
///
/// 格式：
///     # 注释行
///     VERSION|1
///     [BOOKS]
///     isbn|title|author|year|status|borrower|dueDate
///     [LEDGER]
///     isbn|borrower|borrowDate|dueDate|returnDate|returned
///
/// 写入采用「先写临时文件再原子替换」，避免中途失败把原数据截断成半截文件。
class TextStorage {
public:
    explicit TextStorage(std::filesystem::path file);

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return file_; }

    /// 启动加载。文件不存在视作「全新的空库」，返回空快照而非报错。
    [[nodiscard]] LibrarySnapshot load() const;

    /// 保存。父目录不存在会自动创建。
    void save(const std::vector<Book*>& books, const std::list<BorrowRecord>& ledger) const;

    [[nodiscard]] static std::string formatDate(const Date& date);
    [[nodiscard]] static Date        parseDate(std::string_view text);

private:
    std::filesystem::path file_;
};

}  // namespace libms
