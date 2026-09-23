#pragma once

#include "library/Book.hpp"

#include <cstddef>
#include <list>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace libms {

/// 领域层错误：调用方违反前置条件（书不存在、书已借出等）。
class LibraryError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
    using std::runtime_error::operator=;
};

/// 图书馆：图书目录 + 借阅流水。
///
/// 内存策略：Library 独占持有堆上的 Book 对象（裸指针），由析构函数统一 delete。
/// 因为持有所有权，拷贝被显式删除以避免二次释放，移动则完整转移所有权（Rule of 5）。
class Library {
public:
    Library() noexcept = default;
    ~Library();

    Library(const Library&)            = delete;
    Library& operator=(const Library&) = delete;
    Library(Library&& other) noexcept;
    Library& operator=(Library&& other) noexcept;

    // ---- 图书 CRUD ----------------------------------------------------

    /// 新建并接管一本书；ISBN 重复时抛 LibraryError。
    Book& addBook(std::string isbn, std::string title, std::string author, int publishYear);

    /// 从存储层接管一个已存在的 Book（移动语义），仍由本对象负责释放。
    Book& adopt(Book book);

    /// 修改图书信息；书不存在时抛 LibraryError。
    void updateBook(std::string_view isbn, std::string newTitle, std::string newAuthor, int newPublishYear);

    /// 删除图书；书不存在或仍处于借出状态时抛 LibraryError。
    void removeBook(std::string_view isbn);

    /// 按 ISBN 查找；不存在时抛 LibraryError。
    [[nodiscard]] Book&       find(std::string_view isbn);
    [[nodiscard]] const Book& find(std::string_view isbn) const;

    /// 按 ISBN 查找；不存在时返回 nullptr。
    [[nodiscard]] Book*       tryFind(std::string_view isbn) noexcept;
    [[nodiscard]] const Book* tryFind(std::string_view isbn) const noexcept;

    /// 按书名/作者模糊匹配（大小写不敏感），结果按加入顺序返回。
    [[nodiscard]] std::vector<const Book*> searchByKeyword(std::string_view keyword) const;

    // ---- 借还 ---------------------------------------------------------

    /// 借书；书不存在、非在架状态或读者名为空时抛 LibraryError。
    /// 返回本次生成的借阅流水。
    const BorrowRecord& borrow(std::string_view isbn, std::string borrower, int loanDays);

    /// 还书；书不存在或未被借出时抛 LibraryError。返回已闭环的流水。
    BorrowRecord giveBack(std::string_view isbn);

    /// 手工标记状态；书不存在或迁移非法（如需先还书）时抛 LibraryError。
    void markStatus(std::string_view isbn, BookStatus status);

    // ---- 持久化视图 ---------------------------------------------------

    [[nodiscard]] const std::vector<Book*>& books() const noexcept { return books_; }
    [[nodiscard]] const std::list<BorrowRecord>& ledger() const noexcept { return ledger_; }
    void restoreLedger(std::list<BorrowRecord> ledger);

    [[nodiscard]] std::size_t bookCount() const noexcept { return books_.size(); }
    [[nodiscard]] std::size_t borrowedCount() const noexcept;

    /// 当前由 Library 持有、尚未 delete 的堆上 Book 对象数。
    ///
    /// 这是内存语义的自检探针：正常情况下它应当始终等于 bookCount()，
    /// 且当所有 Library 实例析构后回落到 0。单元测试用它验证「析构确实释放了内存」。
    [[nodiscard]] static std::int64_t liveBookObjects() noexcept;

    /// 释放全部图书并清空流水。
    void clear() noexcept;

private:
    /// 堆上 Book 的申请与释放集中在这两个函数里，保证计数器与实际 new/delete 严格配对。
    [[nodiscard]] static Book* allocateBook(Book book);
    static void                releaseBook(Book* book) noexcept;

    [[nodiscard]] Book* mutableFind(std::string_view isbn) noexcept;

    std::vector<Book*>      books_;   ///< 目录，需要按下标随机访问与顺序遍历
    std::list<BorrowRecord> ledger_;  ///< 流水，只在尾部追加、按时间顺序遍历
};

}  // namespace libms
