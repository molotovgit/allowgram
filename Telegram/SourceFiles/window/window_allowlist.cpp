/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_allowlist.h"

#include "core/application.h"
#include "lang/lang_keys.h"
#include "main/allowlist_policy.h"
#include "main/main_session.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/qt_object_factory.h"
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

class AllowlistLockWidget::IdRow final : public Ui::RpWidget {
public:
	IdRow(
		QWidget *parent,
		bool users,
		Fn<void()> add,
		Fn<void(not_null<IdRow*>)> remove);

	[[nodiscard]] not_null<Ui::InputField*> field() const;
	void setRemoveEnabled(bool enabled);

protected:
	int resizeGetHeight(int newWidth) override;

private:
	Ui::InputField *_field = nullptr;
	Ui::LinkButton *_remove = nullptr;

};

AllowlistLockWidget::IdRow::IdRow(
	QWidget *parent,
	bool users,
	Fn<void()> add,
	Fn<void(not_null<IdRow*>)> remove)
: RpWidget(parent)
, _field(Ui::CreateChild<Ui::InputField>(
	this,
	st::allowlistInput,
	Ui::InputField::Mode::SingleLine,
	users ? tr::lng_allowgram_users() : tr::lng_allowgram_groups()))
, _remove(Ui::CreateChild<Ui::LinkButton>(this, QString())) {
	_field->setInputMethodHints(Qt::ImhNoAutoUppercase
		| Qt::ImhNoPredictiveText);
	_field->submits() | rpl::on_next([=] {
		add();
	}, lifetime());
	_remove->setClickedCallback([=] {
		crl::on_main(this, [=] { remove(this); });
	});
	tr::lng_allowgram_remove_id() | rpl::on_next([=](const QString &text) {
		_remove->setText(text);
		resizeToWidth(width());
	}, lifetime());
	_field->show();
	_remove->show();
}

not_null<Ui::InputField*> AllowlistLockWidget::IdRow::field() const {
	return _field;
}

void AllowlistLockWidget::IdRow::setRemoveEnabled(bool enabled) {
	_remove->setDisabled(!enabled);
}

int AllowlistLockWidget::IdRow::resizeGetHeight(int newWidth) {
	const auto removeWidth = std::min(_remove->naturalWidth(), newWidth / 3);
	_remove->resizeToWidth(removeWidth);
	_field->resizeToWidth(std::max(
		newWidth - removeWidth - st::allowlistLabelSkip,
		1));
	_field->moveToLeft(0, 0, newWidth);
	_remove->moveToRight(
		0,
		std::max((_field->height() - _remove->height()) / 2, 0),
		newWidth);
	return _field->height();
}

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
	_layout->add(
		object_ptr<Ui::FlatLabel>(
			_layout,
			tr::lng_allowgram_users(),
			st::allowlistDescription),
		st::allowlistRowPadding);
	Ui::AddSkip(_layout, st::allowlistLabelSkip);
	_usersLayout = _layout->add(
		object_ptr<Ui::VerticalLayout>(_layout),
		st::allowlistRowPadding);
	_addUser = _layout->add(
		object_ptr<Ui::RoundButton>(
			_layout,
			tr::lng_allowgram_add_user(),
			st::allowlistAddButton),
		st::allowlistAddPadding);
	_addUser->setClickedCallback([=] { addRow(true); });
	_layout->add(
		object_ptr<Ui::FlatLabel>(
			_layout,
			tr::lng_allowgram_users_hint(),
			st::allowlistHint),
		st::allowlistHintPadding);
	Ui::AddSkip(_layout, st::allowlistSectionSkip);
	_layout->add(
		object_ptr<Ui::FlatLabel>(
			_layout,
			tr::lng_allowgram_groups(),
			st::allowlistDescription),
		st::allowlistRowPadding);
	Ui::AddSkip(_layout, st::allowlistLabelSkip);
	_groupsLayout = _layout->add(
		object_ptr<Ui::VerticalLayout>(_layout),
		st::allowlistRowPadding);
	_addGroup = _layout->add(
		object_ptr<Ui::RoundButton>(
			_layout,
			tr::lng_allowgram_add_group(),
			st::allowlistAddButton),
		st::allowlistAddPadding);
	_addGroup->setClickedCallback([=] { addRow(false); });
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
	addRow(true, false);
	addRow(false, false);
}

void AllowlistLockWidget::addRow(bool users, bool focus) {
	if (_users.size() + _groups.size() >= Main::Allowlist::kMaximumEntries + 1) {
		showError(tr::lng_allowgram_too_many(tr::now));
		return;
	}
	const auto layout = users ? _usersLayout : _groupsLayout;
	auto &rows = users ? _users : _groups;
	const auto row = layout->add(
		object_ptr<IdRow>(
			layout,
			users,
			[=] { addRow(users); },
			[=](not_null<IdRow*> row) { removeRow(users, row); }),
		st::allowlistIdPadding);
	rows.push_back(row);
	row->field()->changes() | rpl::on_next([=] {
		clearError();
	}, row->lifetime());
	row->field()->focusedChanges() | rpl::filter(
		rpl::mappers::_1
	) | rpl::on_next([=] {
		_scroll->scrollToWidget(row);
	}, row->lifetime());
	refreshRowButtons();
	if (focus) {
		clearError();
		row->field()->setFocus();
		_scroll->scrollToWidget(row);
	}
}

void AllowlistLockWidget::removeRow(bool users, not_null<IdRow*> row) {
	auto &rows = users ? _users : _groups;
	const auto i = ranges::find(rows, row.get());
	if (i == rows.end() || rows.size() == 1) {
		return;
	}
	const auto index = int(i - rows.begin());
	rows.erase(i);
	delete row.get();
	refreshRowButtons();
	clearError();
	const auto next = rows[std::min(index, int(rows.size()) - 1)];
	next->field()->setFocus();
	_scroll->scrollToWidget(next);
}

void AllowlistLockWidget::refreshRowButtons() {
	for (const auto row : _users) {
		row->setRemoveEnabled(_users.size() > 1);
	}
	for (const auto row : _groups) {
		row->setRemoveEnabled(_groups.size() > 1);
	}
	const auto full = (_users.size() + _groups.size()
		>= Main::Allowlist::kMaximumEntries + 1);
	_addUser->setDisabled(full);
	_addGroup->setDisabled(full);
}

QString AllowlistLockWidget::CollectIds(const std::vector<IdRow*> &rows) {
	auto result = QString();
	for (const auto row : rows) {
		if (!result.isEmpty()) {
			result += '\n';
		}
		result += row->field()->getLastText();
	}
	return result;
}

void AllowlistLockWidget::setInnerFocus() {
	_users.front()->field()->setFocus();
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
		CollectIds(_users),
		CollectIds(_groups));
	if (!error.isEmpty()) {
		showError(error);
		return;
	}
	UnlockAllowlistWindows(session);
}

void AllowlistLockWidget::showError(const QString &error) {
	_error->setText(error);
	_error->show();
	_scroll->scrollToWidget(_error);
}

void AllowlistLockWidget::clearError() {
	_error->hide();
}

} // namespace Window
