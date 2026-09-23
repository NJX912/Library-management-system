#pragma once

#include "library/Library.hpp"
#include "library/TextStorage.hpp"

namespace libms {

/// 命令行界面。
///
/// 只负责输入解析、校验与展示，业务规则一律交给 Library；
/// 这样领域逻辑可以脱离终端被单元测试直接驱动。
class ConsoleUI {
public:
    ConsoleUI(Library& library, TextStorage& storage);

    /// 进入主循环，返回进程退出码。
    int run();

private:
    void printMenu() const;
    void printSummary() const;
    void printBooks(const std::vector<const Book*>& books) const;

    void handleAddBook();
    void handleUpdateBook();
    void handleRemoveBook();
    void handleSearch() const;
    void handleBorrow();
    void handleGiveBack();
    void handleMarkStatus();
    void handleShowLedger() const;
    void handleSave() const;

    Library&      library_;
    TextStorage&  storage_;
};

}  // namespace libms
