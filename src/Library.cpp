#include "library/Library.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <utility>

namespace libms {
namespace {

constexpr int kMinLoanDays = 1;
constexpr int kMaxLoanDays = 365;

std::atomic<std::int64_t> g_liveBookObjects{0};

Date today() {
    using namespace std::chrono;
    return year_month_day{floor<days>(system_clock::now())};
}

std::string toUpper(std::string_view text) {
    std::string result{text};
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return result;
}

/// 大小写不敏感的子串匹配（ASCII 语义，足够覆盖 ISBN 与西文书名）。
bool containsFold(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) {
        return true;
    }
    return toUpper(haystack).find(toUpper(needle)) != std::string::npos;
}

bool canTransition(BookStatus from, BookStatus to) noexcept {
    if (from == to) {
        return false;
    }
    if (to == BookStatus::Borrowed) {
        return false;  // 借出只能经由 borrow()
    }
    if (from == BookStatus::Borrowed) {
        return false;  // 借出中必须先还书
    }
    return true;
}

}  // namespace

Library::~Library() {
    clear();
}

Library::Library(Library&& other) noexcept
    : books_{std::move(other.books_)}, ledger_{std::move(other.ledger_)} {
    // 标准只保证 vector 的移动构造后源对象处于「有效但未指定」状态，
    // 显式清空才能真正保证析构时不会重复 delete。
    other.books_.clear();
    other.ledger_.clear();
}

Library& Library::operator=(Library&& other) noexcept {
    if (this != &other) {
        clear();
        books_  = std::move(other.books_);
        ledger_ = std::move(other.ledger_);
        other.books_.clear();
        other.ledger_.clear();
    }
    return *this;
}

void Library::clear() noexcept {
    for (Book* book : books_) {
        releaseBook(book);  // Book 通过 new 分配，由 Library 独占持有
    }
    books_.clear();
    ledger_.clear();
}

Book* Library::allocateBook(Book book) {
    auto* owned = new Book{std::move(book)};
    g_liveBookObjects.fetch_add(1, std::memory_order_relaxed);
    return owned;
}

void Library::releaseBook(Book* book) noexcept {
    delete book;
    g_liveBookObjects.fetch_sub(1, std::memory_order_relaxed);
}

std::int64_t Library::liveBookObjects() noexcept {
    return g_liveBookObjects.load(std::memory_order_relaxed);
}

Book* Library::mutableFind(std::string_view isbn) noexcept {
    const auto it = std::find_if(books_.begin(), books_.end(), [isbn](const Book* book) {
        return book->isbn() == isbn;
    });
    return it == books_.end() ? nullptr : *it;
}

Book* Library::tryFind(std::string_view isbn) noexcept {
    return mutableFind(isbn);
}

const Book* Library::tryFind(std::string_view isbn) const noexcept {
    const auto it = std::find_if(books_.begin(), books_.end(), [isbn](const Book* book) {
        return book->isbn() == isbn;
    });
    return it == books_.end() ? nullptr : *it;
}

Book& Library::find(std::string_view isbn) {
    Book* book = mutableFind(isbn);
    if (book == nullptr) {
        throw LibraryError{"找不到 ISBN 为 " + std::string{isbn} + " 的图书"};
    }
    return *book;
}

const Book& Library::find(std::string_view isbn) const {
    const Book* book = tryFind(isbn);
    if (book == nullptr) {
        throw LibraryError{"找不到 ISBN 为 " + std::string{isbn} + " 的图书"};
    }
    return *book;
}

Book& Library::addBook(std::string isbn, std::string title, std::string author, int publishYear) {
    validateField(isbn, "ISBN");
    if (mutableFind(isbn) != nullptr) {
        throw LibraryError{"ISBN " + isbn + " 已存在"};
    }
    return adopt(Book{std::move(isbn), std::move(title), std::move(author), publishYear});
}

Book& Library::adopt(Book book) {
    if (mutableFind(book.isbn()) != nullptr) {
        throw LibraryError{"ISBN " + book.isbn() + " 已存在"};
    }
    books_.push_back(allocateBook(std::move(book)));
    return *books_.back();
}

void Library::updateBook(std::string_view isbn,
                         std::string      newTitle,
                         std::string      newAuthor,
                         int              newPublishYear) {
    find(isbn).resetInfo(std::move(newTitle), std::move(newAuthor), newPublishYear);
}

void Library::removeBook(std::string_view isbn) {
    Book& book = find(isbn);
    if (book.isBorrowed()) {
        throw LibraryError{"图书《" + book.title() + "》仍处于借出状态，请先还书"};
    }
    const auto it = std::find(books_.begin(), books_.end(), &book);
    releaseBook(*it);  // 先释放，再从容器摘除，避免出现悬垂指针
    books_.erase(it);
}

std::vector<const Book*> Library::searchByKeyword(std::string_view keyword) const {
    std::vector<const Book*> hits;
    for (const Book* book : books_) {
        if (containsFold(book->title(), keyword) || containsFold(book->author(), keyword) ||
            containsFold(book->isbn(), keyword)) {
            hits.push_back(book);
        }
    }
    return hits;
}

const BorrowRecord& Library::borrow(std::string_view isbn, std::string borrower, int loanDays) {
    Book& book = find(isbn);
    if (book.status() != BookStatus::Available) {
        throw LibraryError{"图书《" + book.title() + "》当前状态为「" +
                           std::string{toDisplayName(book.status())} + "」，不可借出"};
    }
    if (loanDays < kMinLoanDays || loanDays > kMaxLoanDays) {
        throw LibraryError{"借期需在 1..365 天之间"};
    }

    const Date borrowDate = today();
    const Date dueDate    = Date{std::chrono::sys_days{borrowDate} + std::chrono::days{loanDays}};
    book.assignBorrower(borrower, dueDate);

    ledger_.push_back(BorrowRecord{
        .isbn       = book.isbn(),
        .borrower   = book.borrower(),
        .borrowDate = borrowDate,
        .dueDate    = dueDate,
        .returnDate = Date{},
        .returned   = false,
    });
    return ledger_.back();
}

BorrowRecord Library::giveBack(std::string_view isbn) {
    Book& book = find(isbn);
    if (!book.isBorrowed()) {
        throw LibraryError{"图书《" + book.title() + "》当前未被借出，无需归还"};
    }

    // 从后往前找该书的未归还流水：正常情况下最多只有一条。
    const auto it = std::find_if(ledger_.rbegin(), ledger_.rend(), [&book](const BorrowRecord& r) {
        return !r.returned && r.isbn == book.isbn();
    });
    if (it == ledger_.rend()) {
        throw LibraryError{"图书《" + book.title() + "》缺少对应的借阅流水，数据不一致"};
    }

    it->returned   = true;
    it->returnDate = today();
    BorrowRecord closed = *it;

    book.clearBorrower();
    book.setStatus(BookStatus::Available);
    return closed;
}

void Library::markStatus(std::string_view isbn, BookStatus status) {
    Book& book = find(isbn);
    if (!canTransition(book.status(), status)) {
        throw LibraryError{"不允许从「" + std::string{toDisplayName(book.status())} + "」变更为「" +
                           std::string{toDisplayName(status)} + "」"};
    }
    book.setStatus(status);
}

void Library::restoreLedger(std::list<BorrowRecord> ledger) {
    ledger_ = std::move(ledger);
}

std::size_t Library::borrowedCount() const noexcept {
    return static_cast<std::size_t>(std::count_if(books_.begin(), books_.end(), [](const Book* book) {
        return book->isBorrowed();
    }));
}

}  // namespace libms
