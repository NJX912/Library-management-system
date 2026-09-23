#include "library/Book.hpp"

#include <stdexcept>
#include <utility>

namespace libms {

std::string_view toDisplayName(BookStatus status) noexcept {
    switch (status) {
        case BookStatus::Available: return "在架";
        case BookStatus::Borrowed:  return "已借出";
        case BookStatus::Lost:      return "遗失";
        case BookStatus::OffShelf:  return "下架";
    }
    return "未知";
}

std::string_view toStorageTag(BookStatus status) noexcept {
    switch (status) {
        case BookStatus::Available: return "AVAILABLE";
        case BookStatus::Borrowed:  return "BORROWED";
        case BookStatus::Lost:      return "LOST";
        case BookStatus::OffShelf:  return "OFF_SHELF";
    }
    return "UNKNOWN";
}

bool parseBookStatus(std::string_view tag, BookStatus& out) noexcept {
    if (tag == "AVAILABLE") { out = BookStatus::Available; return true; }
    if (tag == "BORROWED")  { out = BookStatus::Borrowed;  return true; }
    if (tag == "LOST")      { out = BookStatus::Lost;      return true; }
    if (tag == "OFF_SHELF") { out = BookStatus::OffShelf;  return true; }
    return false;
}

void validateField(std::string_view value, std::string_view fieldName) {
    if (value.empty()) {
        throw std::invalid_argument{std::string{fieldName} + "不能为空"};
    }
    if (value.find('|') != std::string_view::npos ||
        value.find('\n') != std::string_view::npos ||
        value.find('\r') != std::string_view::npos) {
        throw std::invalid_argument{std::string{fieldName} + "不能包含 '|' 或换行符"};
    }
}

Book::Book(std::string isbn,
           std::string title,
           std::string author,
           int         publishYear,
           BookStatus  status)
    : isbn_{std::move(isbn)},
      title_{std::move(title)},
      author_{std::move(author)},
      publishYear_{publishYear},
      status_{status} {
    validateField(isbn_, "ISBN");
    validateField(title_, "书名");
    validateField(author_, "作者");
    if (publishYear_ < 1000 || publishYear_ > 2100) {
        throw std::invalid_argument{"出版年份需在 1000..2100 之间"};
    }
    // 借出状态只能由 borrow() 产生，构造时不允许直接指定。
    if (status_ == BookStatus::Borrowed) {
        throw std::invalid_argument{"不能直接构造为「已借出」，请使用 Library::borrow()"};
    }
}

void Book::resetInfo(std::string title, std::string author, int publishYear) {
    validateField(title, "书名");
    validateField(author, "作者");
    if (publishYear < 1000 || publishYear > 2100) {
        throw std::invalid_argument{"出版年份需在 1000..2100 之间"};
    }
    title_       = std::move(title);
    author_      = std::move(author);
    publishYear_ = publishYear;
}

void Book::assignBorrower(std::string borrower, Date dueDate) {
    validateField(borrower, "读者姓名");
    borrower_ = std::move(borrower);
    dueDate_  = dueDate;
    status_   = BookStatus::Borrowed;
}

void Book::clearBorrower() noexcept {
    borrower_.clear();
    dueDate_ = Date{};
}

void Book::setStatus(BookStatus status) noexcept {
    status_ = status;
}

}  // namespace libms
