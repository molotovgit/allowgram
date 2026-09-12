/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "window/window_lock_widgets.h"

namespace Ui {
class FlatLabel;
class InputField;
class ScrollArea;
class VerticalLayout;
} // namespace Ui

namespace Window {

class AllowlistLockWidget final : public LockWidget {
public:
	AllowlistLockWidget(QWidget *parent, not_null<Controller*> window);

	void setInnerFocus() override;

protected:
	void resizeEvent(QResizeEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;

private:
	void submit();
	void clearError();

	object_ptr<Ui::ScrollArea> _scroll;
	Ui::VerticalLayout *_layout = nullptr;
	Ui::InputField *_users = nullptr;
	Ui::InputField *_groups = nullptr;
	Ui::FlatLabel *_error = nullptr;

};

} // namespace Window
