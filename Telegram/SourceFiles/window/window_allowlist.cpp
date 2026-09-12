/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_allowlist.h"

#include "core/application.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/vertical_list.h"
#include "window/window_controller.h"
#include "mainwindow.h"

#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_window.h"

namespace Window {
namespace {

void UnlockAllowlistWindows(not_null<Main::Session*> session) {
	Core::App().enumerateWindows([=](not_null<Controller*> window) {
		if (window->maybeSession() == session) {
			crl::on_main(window, [=] {
				window->widget()->clearAllowlistLock();
			});
		}
	});
}

} // namespace

AllowlistLockWidget::AllowlistLockWidget(
	QWidget *parent,
	not_null<Controller*> window)
: LockWidget(parent, window)
, _scroll(this, st::defaultSolidScroll)
, _layout(_scroll->setOwnedWidget(
	object_ptr<Ui::VerticalLayout>(_scroll.data())).data()) {
	_layout->add(
		object_ptr<Ui::FlatLabel>(
			_layout,
			tr::lng_allowgram_setup_title(),
			st::allowlistTitle),
		st::allowlistRowPadding);
	Ui::AddSkip(_layout, st::allowlistSectionSkip);
	_layout->add(
		object_ptr<Ui::FlatLabel>(
			_layout,
			tr::lng_allowgram_setup_about(),
			st::allowlistDescription),
		st::allowlistRowPadding);
	Ui::AddSkip(_layout, st::allowlistSectionSkip);
	_users = _layout->add(
		object_ptr<Ui::InputField>(
			_layout,
			st::allowlistInput,
			Ui::InputField::Mode::MultiLine,
			tr::lng_allowgram_users()),
		st::allowlistRowPadding);
	_users->setInputMethodHints(Qt::ImhNoAutoUppercase
		| Qt::ImhNoPredictiveText);
	_layout->add(
		object_ptr<Ui::FlatLabel>(
			_layout,
			tr::lng_allowgram_users_hint(),
			st::allowlistHint),
		st::allowlistHintPadding);
	Ui::AddSkip(_layout, st::allowlistSectionSkip);
	_groups = _layout->add(
		object_ptr<Ui::InputField>(
			_layout,
			st::allowlistInput,
			Ui::InputField::Mode::MultiLine,
			tr::lng_allowgram_groups()),
		st::allowlistRowPadding);
	_groups->setInputMethodHints(Qt::ImhNoAutoUppercase
		| Qt::ImhNoPredictiveText);
	_layout->add(
		object_ptr<Ui::FlatLabel>(
			_layout,
			tr::lng_allowgram_groups_hint(),
			st::allowlistHint),
		st::allowlistHintPadding);
	Ui::AddSkip(_layout, st::allowlistSectionSkip);
	_error = _layout->add(
		object_ptr<Ui::FlatLabel>(
			_layout,
			QString(),
			st::allowlistDescription),
		st::allowlistRowPadding);
	_error->setTextColorOverride(st::boxTextFgError->c);
	_error->hide();
	const auto submit = _layout->add(
		object_ptr<Ui::RoundButton>(
			_layout,
			tr::lng_allowgram_save_continue(),
			st::allowlistSubmit),
		st::allowlistButtonPadding);
	submit->setClickedCallback([=] { this->submit(); });
	const auto logout = _layout->add(
		object_ptr<Ui::RoundButton>(
			_layout,
			tr::lng_settings_logout(),
			st::defaultBoxButton),
		st::allowlistRowPadding);
	logout->setClickedCallback([=] {
		window->showLogoutConfirmation();
	});
	Ui::AddSkip(_layout, st::allowlistSectionSkip);
	_users->changes() | rpl::on_next([=] {
		clearError();
	}, lifetime());
	_groups->changes() | rpl::on_next([=] {
		clearError();
	}, lifetime());
}

void AllowlistLockWidget::setInnerFocus() {
	_users->setFocus();
}

void AllowlistLockWidget::resizeEvent(QResizeEvent *e) {
	const auto layoutWidth = std::min(width(), st::allowlistContentWidth);
	const auto top = st::allowlistContentTop;
	_scroll->setGeometry(
		(width() - layoutWidth) / 2,
		top,
		layoutWidth,
		std::max(height() - top, 0));
	_layout->resizeToWidth(layoutWidth);
}

void AllowlistLockWidget::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape || e->key() == Qt::Key_Back) {
		e->accept();
		return;
	}
	LockWidget::keyPressEvent(e);
}

void AllowlistLockWidget::submit() {
	const auto session = window()->maybeSession();
	if (!session) {
		return;
	}
	if (session->allowlistConfigured()) {
		UnlockAllowlistWindows(session);
		return;
	}
	const auto error = session->configureAllowlist(
		_users->getLastText(),
		_groups->getLastText());
	if (!error.isEmpty()) {
		_error->setText(error);
		_error->show();
		_scroll->scrollToWidget(_error);
		return;
	}
	UnlockAllowlistWindows(session);
}

void AllowlistLockWidget::clearError() {
	_error->hide();
}

} // namespace Window
