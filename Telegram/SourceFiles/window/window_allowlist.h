/* Allowgram: first-login chat selection. Upstream license: see LEGAL. */
#pragma once
#include "window/window_lock_widgets.h"
#include "base/weak_ptr.h"
#include "main/allowlist_picker.h"
#include <memory>
#include <vector>
#include "rpl/variable.h"

class PeerListRow;
class PeerListController;
class PeerListContent;
class PeerListContentDelegateSimple;
namespace Main { class Session; }
namespace MTP { class Sender; }
namespace Ui {
class FlatLabel;
class InputField;
class RoundButton;
class ScrollArea;
class VerticalLayout;
} // namespace Ui

namespace Window {
class AllowlistLockWidget final : public LockWidget {
public:
 AllowlistLockWidget(QWidget *parent, not_null<Controller*> window);
 ~AllowlistLockWidget();
 void setInnerFocus() override;
protected:
 void resizeEvent(QResizeEvent *e) override;
 void keyPressEvent(QKeyEvent *e) override;
private:
 void load();
 void requestNext();
 void receivePage(std::uint64_t generation, Main::Allowlist::Picker::Page page);
 void failed(std::uint64_t generation);
 void clearRows();
 void rebuildRows();
 void updateRowsVisibleRange();
 void updateStatus();
 void submit();
 void showError(const QString &error);
 [[nodiscard]] bool sameSession() const;
 object_ptr<Ui::ScrollArea> _scroll;
 Ui::VerticalLayout *_layout = nullptr;
 Ui::VerticalLayout *_rowsLayout = nullptr;
 Ui::InputField *_search = nullptr;
 Ui::FlatLabel *_status = nullptr;
 Ui::RoundButton *_submit = nullptr;
 Ui::RoundButton *_retry = nullptr;
 Ui::RoundButton *_logout = nullptr;
 std::vector<PeerListRow*> _rows;
 std::unique_ptr<PeerListController> _rowsController;
 std::unique_ptr<PeerListContentDelegateSimple> _rowsDelegate;
 PeerListContent *_rowsContent = nullptr;
 Main::Allowlist::Picker::Model _model;
 std::unique_ptr<MTP::Sender> _api;
 base::weak_ptr<Main::Session> _session;
 std::uint64_t _generation = 0;
 rpl::variable<int> _selectedCount;
};
} // namespace Window
