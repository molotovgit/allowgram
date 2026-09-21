/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_allowlist.h"

#include "boxes/peer_list_box.h"
#include "core/application.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/allowlist_policy.h"
#include "main/main_session.h"
#include "mtproto/sender.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
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

class ChatsController final : public PeerListController {
public:
	explicit ChatsController(not_null<Main::Session*> session)
	: _session(session) {
	}

	Main::Session &session() const override {
		return *_session;
	}
	void prepare() override {
		delegate()->peerListSetSearchMode(PeerListSearchMode::Enabled);
	}
	void rowClicked(not_null<PeerListRow*> row) override {
		delegate()->peerListSetRowChecked(row, !row->checked());
		_checkedChanges.fire({});
	}

	[[nodiscard]] rpl::producer<> checkedChanges() const {
		return _checkedChanges.events();
	}

private:
	const not_null<Main::Session*> _session;
	rpl::event_stream<> _checkedChanges;

};

class ChatsDelegate final : public PeerListContentDelegateSimple {
public:
	bool peerListIsRowChecked(not_null<PeerListRow*> row) override {
		return row->checked();
	}

};

} // namespace

class AllowlistLockWidget::ChatPicker final : public Ui::VerticalLayout {
public:
	ChatPicker(QWidget *parent, not_null<Main::Session*> session);
	[[nodiscard]] QString selectedIds(bool users) const;
	[[nodiscard]] rpl::producer<int> selectedCountValue() const;
	[[nodiscard]] int listHeight() const;
	void setListHeight(int height);
	void focusSearch();

private:
	void loadMore();
	void addPeer(not_null<PeerData*> peer);
	void refreshStatus();
	void loadFailed();

	not_null<Main::Session*> _session;
	MTP::Sender _api;
	const not_null<ChatsController*> _controller;
	const not_null<ChatsDelegate*> _delegate;
	Ui::InputField *_search = nullptr;
	Ui::FlatLabel *_status = nullptr;
	Ui::RpWidget *_viewport = nullptr;
	Ui::ScrollArea *_scroll = nullptr;
	PeerListContent *_content = nullptr;
	Ui::RoundButton *_retry = nullptr;
	rpl::variable<int> _selectedCount;
	base::flat_set<PeerId> _seen;
	int _folder = 0;
	TimeId _offsetDate = 0;
	MsgId _offsetId = 0;
	PeerId _offsetPeer;
	bool _loading = false;
	bool _complete = false;

};

AllowlistLockWidget::ChatPicker::ChatPicker(
	QWidget *parent,
	not_null<Main::Session*> session)
: VerticalLayout(parent)
, _session(session)
, _api(&session->mtp())
, _controller(lifetime().make_state<ChatsController>(session))
, _delegate(lifetime().make_state<ChatsDelegate>()) {
	_search = add(object_ptr<Ui::InputField>(
		this,
		st::allowlistInput,
		Ui::InputField::Mode::SingleLine,
		tr::lng_allowgram_search_chats()), st::allowlistRowPadding);
	_search->setDocumentMargin(st::allowlistInput.border);
	_status = add(object_ptr<Ui::FlatLabel>(
		this,
		tr::lng_allowgram_loading_chats(),
		st::allowlistHint), st::allowlistHintPadding);
	_viewport = add(
		object_ptr<Ui::RpWidget>(this),
		st::allowlistListPadding);
	_viewport->resize(0, st::allowlistChatListHeight);
	_scroll = Ui::CreateChild<Ui::ScrollArea>(
		_viewport,
		st::defaultScrollArea);
	_content = _scroll->setOwnedWidget(
		object_ptr<PeerListContent>(_scroll, _controller)).data();
	_delegate->setContent(_content);
	_controller->setDelegate(_delegate);
	_delegate->peerListSetSearchNoResults(object_ptr<Ui::FlatLabel>(
		nullptr,
		tr::lng_bot_chats_not_found(),
		st::membersAbout));
	_viewport->sizeValue() | rpl::on_next([=](QSize size) {
		_scroll->setGeometry(QRect(QPoint(), size));
		_content->resizeToWidth(size.width());
	}, _viewport->lifetime());
	rpl::combine(
		_scroll->scrollTopValue(),
		_scroll->heightValue()
	) | rpl::on_next([=](int top, int height) {
		_content->setVisibleTopBottom(top, top + height);
	}, _scroll->lifetime());
	_scroll->show();
	_search->changes() | rpl::on_next([=] {
		_content->searchQueryChanged(_search->getLastText());
		_scroll->scrollToY(0);
	}, lifetime());
	_controller->checkedChanges() | rpl::on_next([=] {
		refreshStatus();
	}, lifetime());
	_retry = add(object_ptr<Ui::RoundButton>(
		this,
		tr::lng_allowgram_retry_chats(),
		st::allowlistAddButton), st::allowlistAddPadding);
	_retry->hide();
	_retry->setClickedCallback([=] { loadMore(); });
	crl::on_main(this, [=] { loadMore(); });
}

void AllowlistLockWidget::ChatPicker::focusSearch() {
	_search->setFocus();
}

rpl::producer<int> AllowlistLockWidget::ChatPicker::selectedCountValue() const {
	return _selectedCount.value();
}

int AllowlistLockWidget::ChatPicker::listHeight() const {
	return _viewport->height();
}

void AllowlistLockWidget::ChatPicker::setListHeight(int height) {
	if (_viewport->height() != height) {
		_viewport->resize(_viewport->width(), height);
	}
}

QString AllowlistLockWidget::ChatPicker::selectedIds(bool users) const {
	auto result = QString();
	const auto count = _delegate->peerListFullRowsCount();
	for (auto i = 0; i != count; ++i) {
		const auto row = _delegate->peerListRowAt(i);
		const auto id = row->peer()->id;
		if (!row->checked() || id.is<UserId>() != users) {
			continue;
		}
		result += id.is<UserId>()
			? u"user:%1\n"_q.arg(peerToUser(id).bare)
			: id.is<ChatId>()
			? u"chat:%1\n"_q.arg(peerToChat(id).bare)
			: u"channel:%1\n"_q.arg(peerToChannel(id).bare);
	}
	return result;
}

void AllowlistLockWidget::ChatPicker::addPeer(not_null<PeerData*> peer) {
	if (_seen.contains(peer->id)
		|| peer->isSelf()
		|| (!peer->id.is<UserId>()
			&& !peer->id.is<ChatId>()
			&& !peer->id.is<ChannelId>())) {
		return;
	}
	_seen.emplace(peer->id);
	_delegate->peerListAppendRow(std::make_unique<PeerListRow>(peer));
}

void AllowlistLockWidget::ChatPicker::refreshStatus() {
	const auto count = _delegate->peerListFullRowsCount();
	auto selected = 0;
	for (auto i = 0; i != count; ++i) {
		selected += _delegate->peerListRowAt(i)->checked() ? 1 : 0;
	}
	_selectedCount = selected;
	if (_complete) {
		_status->setText(tr::lng_allowgram_chats_ready(
			tr::now,
			lt_ready,
			QString::number(count),
			lt_total,
			QString::number(selected)));
	}
}

void AllowlistLockWidget::ChatPicker::loadFailed() {
	_loading = false;
	_status->setText(tr::lng_allowgram_chats_failed(tr::now));
	_retry->show();
}

void AllowlistLockWidget::ChatPicker::loadMore() {
	if (_loading || _complete || _session->allowlistConfigured()) {
		return;
	}
	_loading = true;
	_retry->hide();
	_status->setText(tr::lng_allowgram_loading_chats(tr::now));
	auto flags = MTPmessages_GetDialogs::Flags(
		MTPmessages_GetDialogs::Flag::f_folder_id);
	if (_offsetPeer) {
		flags |= MTPmessages_GetDialogs::Flag::f_exclude_pinned;
	}
	_api.request(MTPmessages_GetDialogs(
		MTP_flags(flags),
		MTP_int(_folder),
		MTP_int(_offsetDate),
		MTP_int(_offsetId),
		_offsetPeer
			? _session->data().peer(_offsetPeer)->input()
			: MTP_inputPeerEmpty(),
		MTP_int(100),
		MTP_long(0)
	)).done([=](const MTPmessages_Dialogs &result) {
		_loading = false;
		result.match([&](const MTPDmessages_dialogsNotModified &) {
			loadFailed();
		}, [&](const auto &data) {
			_session->data().processUsers(data.vusers());
			_session->data().processChats(data.vchats());
			auto lastPeer = PeerId();
			auto lastId = MsgId(0);
			auto lastDate = TimeId(0);
			for (const auto &dialog : data.vdialogs().v) {
				dialog.match([&](const MTPDdialog &entry) {
					const auto id = peerFromMTP(entry.vpeer());
					if (const auto peer = _session->data().peerLoaded(id)) {
						addPeer(peer);
					}
					if (entry.is_pinned()) {
						return;
					}
					for (const auto &message : data.vmessages().v) {
						if (PeerFromMessage(message) == id
							&& IdFromMessage(message) == entry.vtop_message().v
							&& DateFromMessage(message)) {
							lastPeer = id;
							lastId = IdFromMessage(message);
							lastDate = DateFromMessage(message);
							break;
						}
					}
				}, [](const auto &) {});
			}
			_delegate->peerListRefreshRows();
			const auto finished = (result.type() == mtpc_messages_dialogs)
				|| !lastPeer;
			if (!finished) {
				if (lastPeer == _offsetPeer
					&& lastId == _offsetId
					&& lastDate == _offsetDate) {
					loadFailed();
					refreshStatus();
					return;
				}
				_offsetPeer = lastPeer;
				_offsetId = lastId;
				_offsetDate = lastDate;
			} else if (_folder == 0) {
				_folder = 1;
				_offsetPeer = PeerId();
				_offsetId = 0;
				_offsetDate = 0;
			} else {
				_complete = true;
			}
			refreshStatus();
			crl::on_main(this, [=] { loadMore(); });
		});
	}).fail([=] {
		loadFailed();
	}).send();
}

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
	_field->setDocumentMargin(st::allowlistInput.border);
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
	const auto textRect = _field->rect().marginsRemoved(
		_field->fullTextMargins());
	_remove->moveToRight(
		0,
		std::max(textRect.y()
			+ (textRect.height() - _remove->height()) / 2, 0),
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
	if (const auto session = window->maybeSession()) {
		_picker = _layout->add(object_ptr<ChatPicker>(_layout, session));
		Ui::AddSkip(_layout, st::allowlistSectionSkip);
	}
	const auto manual = _layout->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			_layout,
			object_ptr<Ui::VerticalLayout>(_layout)));
	const auto form = manual->entity();
	if (_picker) {
		const auto toggle = _layout->add(object_ptr<Ui::RoundButton>(
			_layout,
			tr::lng_allowgram_manual_ids(),
			st::allowlistAddButton), st::allowlistAddPadding);
		manual->toggle(false, anim::type::instant);
		toggle->setClickedCallback([=] {
			manual->toggle(!manual->toggled(), anim::type::normal);
		});
	}
	form->add(
		object_ptr<Ui::FlatLabel>(
			form,
			tr::lng_allowgram_users(),
			st::allowlistDescription),
		st::allowlistRowPadding);
	Ui::AddSkip(form, st::allowlistLabelSkip);
	_usersLayout = form->add(
		object_ptr<Ui::VerticalLayout>(form),
		st::allowlistRowPadding);
	_addUser = form->add(
		object_ptr<Ui::RoundButton>(
			form,
			tr::lng_allowgram_add_user(),
			st::allowlistAddButton),
		st::allowlistAddPadding);
	_addUser->setClickedCallback([=] { addRow(true); });
	form->add(
		object_ptr<Ui::FlatLabel>(
			form,
			tr::lng_allowgram_users_hint(),
			st::allowlistHint),
		st::allowlistHintPadding);
	Ui::AddSkip(form, st::allowlistSectionSkip);
	form->add(
		object_ptr<Ui::FlatLabel>(
			form,
			tr::lng_allowgram_groups(),
			st::allowlistDescription),
		st::allowlistRowPadding);
	Ui::AddSkip(form, st::allowlistLabelSkip);
	_groupsLayout = form->add(
		object_ptr<Ui::VerticalLayout>(form),
		st::allowlistRowPadding);
	_addGroup = form->add(
		object_ptr<Ui::RoundButton>(
			form,
			tr::lng_allowgram_add_group(),
			st::allowlistAddButton),
		st::allowlistAddPadding);
	_addGroup->setClickedCallback([=] { addRow(false); });
	form->add(
		object_ptr<Ui::FlatLabel>(
			form,
			tr::lng_allowgram_groups_hint(),
			st::allowlistHint),
		st::allowlistHintPadding);
	Ui::AddSkip(form, st::allowlistSectionSkip);
	_error = _layout->add(
		object_ptr<Ui::FlatLabel>(
			_layout,
			QString(),
			st::allowlistDescription),
		st::allowlistRowPadding);
	_error->setTextColorOverride(st::boxTextFgError->c);
	_error->hide();
	auto submitText = _picker
		? rpl::producer<QString>(_picker->selectedCountValue(
		) | rpl::map([](int count) {
			return count
				? tr::lng_allowgram_save_count(tr::now, lt_count, count)
				: tr::lng_allowgram_save_continue(tr::now);
		}))
		: tr::lng_allowgram_save_continue();
	const auto submit = _layout->add(
		object_ptr<Ui::RoundButton>(
			_layout,
			std::move(submitText),
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
	_layout->heightValue() | rpl::on_next([=] {
		updatePickerHeight();
	}, lifetime());
}

void AllowlistLockWidget::updatePickerHeight() {
	if (!_picker) {
		return;
	}
	const auto other = _layout->height() - _picker->listHeight();
	const auto available = height() - st::allowlistContentTop - other;
	_picker->setListHeight(std::max(available, st::allowlistChatListHeight));
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
	if (_picker) {
		_picker->focusSearch();
	} else {
		_users.front()->field()->setFocus();
	}
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
	updatePickerHeight();
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
		(_picker ? _picker->selectedIds(true) : QString()) + CollectIds(_users),
		(_picker ? _picker->selectedIds(false) : QString()) + CollectIds(_groups));
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
