#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

namespace libms {

/// 借阅日期统一用 chrono 的日历类型表示，天然可比较、可做天数运算。
using Date = std::chrono::year_month_day;

/// 图书状态。数值是持久化格式的一部分，不要随意改动已有取值。
enum class BookStatus : std::uint8_t {
    Available = 0,  ///< 在架可借
    Borrowed  = 1,  ///< 已借出
    Lost      = 2,  ///< 遗失
    OffShelf  = 3,  ///< 下架
};

/// 面向控制台的中文名称。
std::string_view toDisplayName(BookStatus status) noexcept;

/// 面向存储的稳定标识，与语言无关。
std::string_view toStorageTag(BookStatus status) noexcept;

/// 从存储标识反解状态；失败返回 false，out 不被修改。
bool parseBookStatus(std::string_view tag, BookStatus& out) noexcept;

/// 一条借阅流水。returned 为 false 时 returnDate 无意义。
struct BorrowRecord {
    std::string isbn;
    std::string borrower;
    Date        borrowDate{};
    Date        dueDate{};
    Date        returnDate{};
    bool        returned{false};
};

/// 一本书。
///
/// 值语义类型：只持有 std::string 与平凡成员，因此拷贝/移动全部交给编译器生成
/// （Rule of 0），不需要手写析构函数。堆上的 Book 由 Library 统一持有并释放。
class Book {
public:
    Book(std::string isbn,
         std::string title,
         std::string author,
         int         publishYear,
         BookStatus  status = BookStatus::Available);

    [[nodiscard]] const std::string& isbn()        const noexcept { return isbn_; }
    [[nodiscard]] const std::string& title()       const noexcept { return title_; }
    [[nodiscard]] const std::string& author()      const noexcept { return author_; }
    [[nodiscard]] int                publishYear() const noexcept { return publishYear_; }
    [[nodiscard]] BookStatus         status()      const noexcept { return status_; }
    [[nodiscard]] const std::string& borrower()    const noexcept { return borrower_; }
    [[nodiscard]] const Date&        dueDate()     const noexcept { return dueDate_; }

    [[nodiscard]] bool isBorrowed() const noexcept { return status_ == BookStatus::Borrowed; }

    /// 供 UI 在修改图书信息时使用，保持 ISBN 不变。
    void resetInfo(std::string title, std::string author, int publishYear);

    void assignBorrower(std::string borrower, Date dueDate);
    void clearBorrower() noexcept;
    void setStatus(BookStatus status) noexcept;

private:
    std::string isbn_;
    std::string title_;
    std::string author_;
    int         publishYear_;
    BookStatus  status_;
    std::string borrower_;
    Date        dueDate_{};
};

/// 字段里出现这些字符会破坏 | 分隔的文本格式，在边界处直接拒绝。
void validateField(std::string_view value, std::string_view fieldName);

}  // namespace libms
