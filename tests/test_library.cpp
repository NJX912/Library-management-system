// 极简测试框架：不引入任何第三方依赖，直接编译进 CTest。
#include "library/Library.hpp"
#include "library/TextStorage.hpp"

#include <cstdint>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

int g_checks    = 0;
int g_failures  = 0;
std::string g_case;

void reportFailure(const char* file, int line, const std::string& what) {
    ++g_failures;
    std::cout << "    [FAIL] " << g_case << " @ " << file << ':' << line << " —— " << what << '\n';
}

#define CHECK(cond)                                                                    \
    do {                                                                               \
        ++g_checks;                                                                    \
        if (!(cond)) reportFailure(__FILE__, __LINE__, "断言失败: " #cond);            \
    } while (false)

#define CHECK_EQ(actual, expected)                                                     \
    do {                                                                               \
        ++g_checks;                                                                    \
        if (!((actual) == (expected))) {                                               \
            reportFailure(__FILE__, __LINE__, std::format("期望 {}，实际 {}",          \
                                                          (expected), (actual)));      \
        }                                                                              \
    } while (false)

/// 断言表达式抛出指定类型的异常。
#define CHECK_THROWS_AS(expr, ExceptionType)                                           \
    do {                                                                               \
        ++g_checks;                                                                    \
        bool threw = false;                                                            \
        try {                                                                          \
            (void)(expr);                                                              \
        } catch (const ExceptionType&) {                                               \
            threw = true;                                                              \
        } catch (const std::exception& unexpected) {                                   \
            reportFailure(__FILE__, __LINE__,                                          \
                          std::format("期望 {}，实际抛出 {}", #ExceptionType,           \
                                      unexpected.what()));                             \
        }                                                                              \
        if (!threw) reportFailure(__FILE__, __LINE__, "期望抛出 " #ExceptionType);      \
    } while (false)

/// 断言表达式会抛 LibraryError（领域规则错误）。
#define CHECK_THROWS(expr) CHECK_THROWS_AS(expr, libms::LibraryError)

void testCase(const std::string& name, void (*fn)()) {
    g_case = name;
    std::cout << "  -- " << name << '\n';
    try {
        fn();
    } catch (const std::exception& error) {
        reportFailure(__FILE__, __LINE__, std::string{"未捕获异常: "} + error.what());
    }
}

/// 每个测试用独立临时文件，互不干扰。
std::filesystem::path tempFile(const std::string& tag) {
    auto path = std::filesystem::temp_directory_path() / ("libms_test_" + tag + ".txt");
    std::filesystem::remove(path);
    return path;
}

using libms::Book;
using libms::BookStatus;
using libms::Library;
using libms::LibraryError;
using libms::LibrarySnapshot;
using libms::TextStorage;

// ---- 用例 ----------------------------------------------------------------

void testAddAndFind() {
    Library library;
    library.addBook("978-7-121-34150-5", "深入理解计算机系统", "Randal Bryant", 2016);
    library.addBook("978-0-13-235088-4", "Clean Code", "Robert Martin", 2008);

    CHECK_EQ(library.bookCount(), 2U);
    CHECK_EQ(library.find("978-7-121-34150-5").title(), std::string{"深入理解计算机系统"});
    CHECK(library.tryFind("不存在的ISBN") == nullptr);
    CHECK_THROWS(library.find("不存在的ISBN"));
}

void testDuplicateIsbnRejected() {
    Library library;
    library.addBook("ISBN-1", "A", "B", 2000);
    CHECK_THROWS(library.addBook("ISBN-1", "C", "D", 2001));
    CHECK_EQ(library.bookCount(), 1U);
}

void testBorrowAndReturn() {
    Library library;
    library.addBook("ISBN-1", "现代操作系统", "Tanenbaum", 2015);

    const auto& record = library.borrow("ISBN-1", "张三", 30);
    CHECK_EQ(record.borrower, std::string{"张三"});
    CHECK(!record.returned);
    CHECK(library.find("ISBN-1").isBorrowed());
    CHECK_EQ(library.find("ISBN-1").borrower(), std::string{"张三"});
    CHECK_EQ(library.borrowedCount(), 1U);
    CHECK_EQ(library.ledger().size(), 1U);

    // 已借出的书不能再借
    CHECK_THROWS(library.borrow("ISBN-1", "李四", 30));

    const auto closed = library.giveBack("ISBN-1");
    CHECK(closed.returned);
    CHECK_EQ(closed.borrower, std::string{"张三"});
    CHECK(closed.returnDate.ok());
    CHECK(!library.find("ISBN-1").isBorrowed());
    CHECK_EQ(library.find("ISBN-1").borrower(), std::string{});
    CHECK_EQ(library.borrowedCount(), 0U);

    // 未借出的书不能归还
    CHECK_THROWS(library.giveBack("ISBN-1"));

    // 归还后可以再次借出，流水追加而非覆盖
    library.borrow("ISBN-1", "王五", 7);
    CHECK_EQ(library.ledger().size(), 2U);
}

void testBorrowValidation() {
    Library library;
    library.addBook("ISBN-1", "编译原理", "Aho", 2009);

    // 参数本身不合法 -> std::invalid_argument
    CHECK_THROWS_AS(library.borrow("ISBN-1", "", 30), std::invalid_argument);
    // 领域规则不满足 -> LibraryError
    CHECK_THROWS(library.borrow("ISBN-1", "张三", 0));
    CHECK_THROWS(library.borrow("ISBN-1", "张三", 400));
    CHECK_THROWS(library.borrow("缺失", "张三", 30));
    CHECK_EQ(library.ledger().size(), 0U);
}

void testStatusTransitions() {
    Library library;
    library.addBook("ISBN-1", "算法导论", "Cormen", 2013);

    // 在架 -> 下架 -> 在架
    library.markStatus("ISBN-1", BookStatus::OffShelf);
    CHECK(library.find("ISBN-1").status() == BookStatus::OffShelf);
    library.markStatus("ISBN-1", BookStatus::Available);
    CHECK(library.find("ISBN-1").status() == BookStatus::Available);

    // 下架的书不能借出
    library.markStatus("ISBN-1", BookStatus::OffShelf);
    CHECK_THROWS(library.borrow("ISBN-1", "张三", 30));
    library.markStatus("ISBN-1", BookStatus::Available);

    // 借出中的书必须先还书才能变更状态
    library.borrow("ISBN-1", "张三", 30);
    CHECK_THROWS(library.markStatus("ISBN-1", BookStatus::Lost));
    CHECK_THROWS(library.markStatus("ISBN-1", BookStatus::Available));

    // 不能手工标记为「已借出」，也不能原地重复标记
    CHECK_THROWS(library.markStatus("ISBN-1", BookStatus::Borrowed));
    library.giveBack("ISBN-1");
    CHECK_THROWS(library.markStatus("ISBN-1", BookStatus::Available));

    // 遗失后仍可恢复在架
    library.markStatus("ISBN-1", BookStatus::Lost);
    CHECK(library.find("ISBN-1").status() == BookStatus::Lost);
    library.markStatus("ISBN-1", BookStatus::Available);
    CHECK(library.find("ISBN-1").status() == BookStatus::Available);
}

void testRemoveAndUpdate() {
    Library library;
    library.addBook("ISBN-1", "旧书名", "旧作者", 2000);

    library.updateBook("ISBN-1", "新书名", "新作者", 2020);
    CHECK_EQ(library.find("ISBN-1").title(), std::string{"新书名"});
    CHECK_EQ(library.find("ISBN-1").publishYear(), 2020);
    CHECK_THROWS(library.updateBook("缺失", "x", "y", 2000));

    library.borrow("ISBN-1", "张三", 30);
    CHECK_THROWS(library.removeBook("ISBN-1"));  // 借出中不可删除

    library.giveBack("ISBN-1");
    library.removeBook("ISBN-1");
    CHECK_EQ(library.bookCount(), 0U);
    CHECK_THROWS(library.removeBook("ISBN-1"));
}

void testSearchIsCaseInsensitive() {
    Library library;
    library.addBook("ISBN-1", "Effective Modern C++", "Scott Meyers", 2015);
    library.addBook("ISBN-2", "Effective Java", "Joshua Bloch", 2018);

    CHECK_EQ(library.searchByKeyword("effective").size(), 2U);
    CHECK_EQ(library.searchByKeyword("EFFECTIVE").size(), 2U);
    CHECK_EQ(library.searchByKeyword("meyers").size(), 1U);
    CHECK_EQ(library.searchByKeyword("java").size(), 1U);
    CHECK_EQ(library.searchByKeyword("").size(), 2U);       // 空关键字返回全部
    CHECK_EQ(library.searchByKeyword("不存在").size(), 0U);
}

void testSaveAndLoadRoundTrip() {
    const auto file = tempFile("roundtrip");
    const TextStorage storage{file};

    {
        Library library;
        library.addBook("ISBN-1", "重构：改善既有代码的设计", "Martin Fowler", 2019);
        library.addBook("ISBN-2", "设计模式", "GoF", 1994);
        library.borrow("ISBN-2", "张三", 14);
        storage.save(library.books(), library.ledger());
    }

    CHECK(std::filesystem::exists(file));

    Library reloaded;
    const LibrarySnapshot snapshot = storage.load();
    for (auto& book : snapshot.books) {
        reloaded.adopt(std::move(book));
    }
    reloaded.restoreLedger(std::move(snapshot.ledger));

    CHECK_EQ(reloaded.bookCount(), 2U);
    CHECK_EQ(reloaded.ledger().size(), 1U);

    const Book& borrowed = reloaded.find("ISBN-2");
    CHECK(borrowed.isBorrowed());
    CHECK_EQ(borrowed.borrower(), std::string{"张三"});
    CHECK_EQ(borrowed.title(), std::string{"设计模式"});
    CHECK_EQ(borrowed.publishYear(), 1994);
    CHECK(borrowed.dueDate().ok());

    const Book& available = reloaded.find("ISBN-1");
    CHECK(!available.isBorrowed());
    CHECK(available.status() == BookStatus::Available);

    std::filesystem::remove(file);
}

void testLoadMissingFileYieldsEmptyLibrary() {
    const auto file = tempFile("missing");
    const TextStorage storage{file};
    const LibrarySnapshot snapshot = storage.load();
    CHECK(snapshot.books.empty());
    CHECK(snapshot.ledger.empty());
}

void testLoadRejectsCorruptedFile() {
    const auto file = tempFile("corrupt");
    {
        std::ofstream out{file};
        out << "VERSION|1\n[BOOKS]\nnot-enough-fields\n";
    }
    const TextStorage storage{file};
    bool threw = false;
    try {
        (void)storage.load();
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);
    std::filesystem::remove(file);
}

void testDestructorReleasesHeapMemory() {
    const std::int64_t baseline = Library::liveBookObjects();

    {
        Library library;
        library.addBook("ISBN-1", "A", "B", 2000);
        library.addBook("ISBN-2", "C", "D", 2001);
        CHECK_EQ(Library::liveBookObjects(), baseline + 2);
    }  // Library 析构

    CHECK_EQ(Library::liveBookObjects(), baseline);  // 堆上的 Book 已被 delete
}

void testClearReleasesHeapMemory() {
    const std::int64_t baseline = Library::liveBookObjects();
    Library library;
    library.addBook("ISBN-1", "A", "B", 2000);
    library.addBook("ISBN-2", "C", "D", 2001);
    CHECK_EQ(Library::liveBookObjects(), baseline + 2);

    library.clear();
    CHECK_EQ(library.bookCount(), 0U);
    CHECK_EQ(Library::liveBookObjects(), baseline);
}

void testMoveTransfersOwnershipExactlyOnce() {
    const std::int64_t baseline = Library::liveBookObjects();

    {
        Library source;
        source.addBook("ISBN-1", "A", "B", 2000);
        source.addBook("ISBN-2", "C", "D", 2001);
        source.borrow("ISBN-1", "张三", 30);

        Library moved{std::move(source)};
        CHECK_EQ(moved.bookCount(), 2U);
        CHECK_EQ(moved.ledger().size(), 1U);
        CHECK_EQ(Library::liveBookObjects(), baseline + 2);  // 转移而非复制
        CHECK_EQ(source.bookCount(), 0U);                    // 源对象已被掏空
        CHECK(moved.find("ISBN-1").isBorrowed());
    }

    CHECK_EQ(Library::liveBookObjects(), baseline);  // 只释放一次，没有二次 delete
}

void testRemoveReleasesOnlyThatBook() {
    const std::int64_t baseline = Library::liveBookObjects();
    Library library;
    library.addBook("ISBN-1", "A", "B", 2000);
    library.addBook("ISBN-2", "C", "D", 2001);
    library.removeBook("ISBN-1");

    CHECK_EQ(Library::liveBookObjects(), baseline + 1);
    CHECK(library.tryFind("ISBN-1") == nullptr);
    CHECK(library.tryFind("ISBN-2") != nullptr);
}

void testFieldValidationRejectsSeparator() {
    Library library;
    bool threw = false;
    try {
        library.addBook("ISBN|1", "书名", "作者", 2000);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);
    CHECK_EQ(library.bookCount(), 0U);
}

void testPersistenceKeepsChineseAndRoundTripsStatus() {
    const auto file = tempFile("utf8");
    const TextStorage storage{file};

    {
        Library library;
        library.addBook("ISBN-1", "红楼梦", "曹雪芹", 1791);
        library.markStatus("ISBN-1", BookStatus::OffShelf);
        storage.save(library.books(), library.ledger());
    }

    Library reloaded;
    const LibrarySnapshot snapshot = storage.load();
    for (auto& book : snapshot.books) {
        reloaded.adopt(std::move(book));
    }

    CHECK_EQ(reloaded.find("ISBN-1").title(), std::string{"红楼梦"});
    CHECK_EQ(reloaded.find("ISBN-1").author(), std::string{"曹雪芹"});
    CHECK(reloaded.find("ISBN-1").status() == BookStatus::OffShelf);

    std::filesystem::remove(file);
}

}  // namespace

int main() {
    std::cout << "运行 libms 单元测试\n";

    testCase("新增与查找图书", testAddAndFind);
    testCase("重复 ISBN 被拒绝", testDuplicateIsbnRejected);
    testCase("借书与还书", testBorrowAndReturn);
    testCase("借书参数校验", testBorrowValidation);
    testCase("状态标记与合法迁移", testStatusTransitions);
    testCase("删除与修改图书", testRemoveAndUpdate);
    testCase("搜索大小写不敏感", testSearchIsCaseInsensitive);
    testCase("保存/加载往返一致", testSaveAndLoadRoundTrip);
    testCase("文件不存在视作空库", testLoadMissingFileYieldsEmptyLibrary);
    testCase("损坏文件被拒绝", testLoadRejectsCorruptedFile);
    testCase("析构释放堆内存", testDestructorReleasesHeapMemory);
    testCase("clear 释放堆内存", testClearReleasesHeapMemory);
    testCase("移动语义不重复释放", testMoveTransfersOwnershipExactlyOnce);
    testCase("删除单本只释放该书", testRemoveReleasesOnlyThatBook);
    testCase("字段校验拒绝分隔符", testFieldValidationRejectsSeparator);
    testCase("中文与状态持久化往返", testPersistenceKeepsChineseAndRoundTripsStatus);

    std::cout << "\n共 " << g_checks << " 项断言，失败 " << g_failures << " 项。\n";
    if (g_failures == 0) {
        std::cout << "全部通过。\n";
        return 0;
    }
    std::cout << "存在失败用例。\n";
    return 1;
}
