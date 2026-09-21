/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "window/window_lock_widgets.h"

#include "base/weak_ptr.h"

#include <memory>

namespace Main {
class Session;
namespace Allowlist::Sheet {
class Resolver;
struct Result;
} // namespace Allowlist::Sheet
} // namespace Main

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
	class IdRow;

	void resolve();
	void resolved(Main::Allowlist::Sheet::Result result);
	void showManual();
	void addLogout();
	[[nodiscard]] bool sameSession() const;
	void addRow(bool users, bool focus = true);
	void removeRow(bool users, not_null<IdRow*> row);
	void refreshRowButtons();
	void submit();
	void showError(const QString &error);
	void clearError();
	static QString CollectIds(const std::vector<IdRow*> &rows);

	object_ptr<Ui::ScrollArea> _scroll;
	Ui::VerticalLayout *_layout = nullptr;
	Ui::VerticalLayout *_usersLayout = nullptr;
	Ui::VerticalLayout *_groupsLayout = nullptr;
	std::vector<IdRow*> _users;
	std::vector<IdRow*> _groups;
	Ui::RoundButton *_addUser = nullptr;
	Ui::RoundButton *_addGroup = nullptr;
	Ui::FlatLabel *_error = nullptr;
	Ui::RoundButton *_retry = nullptr;
	std::unique_ptr<Main::Allowlist::Sheet::Resolver> _resolver;
	base::weak_ptr<Main::Session> _session;
	QString _phone;
	bool _manual = false;

};

} // namespace Window
