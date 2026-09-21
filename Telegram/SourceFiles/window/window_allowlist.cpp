/* Allowgram: first-login chat selection. Upstream license: see LEGAL. */
#include "window/window_allowlist.h"
#include <algorithm>
#include <vector>
#include "core/application.h"
#include "data/data_user.h"
#include "data/data_chat.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "data/data_types.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "mtproto/sender.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
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
using Entry = Main::Allowlist::Entry;
using Kind = Main::Allowlist::Kind;
using Page = Main::Allowlist::Picker::Page;

PeerId ToPeer(Entry entry) {
 switch (entry.kind) {
 case Kind::User: return peerFromUser(UserId(entry.id));
 case Kind::Chat: return peerFromChat(ChatId(entry.id));
 case Kind::Channel: return peerFromChannel(ChannelId(entry.id));
 }
 return PeerId();
}
Entry ToEntry(PeerId peer) {
 if (peerIsUser(peer)) return { Kind::User, peerToUser(peer).bare };
 if (peerIsChat(peer)) return { Kind::Chat, peerToChat(peer).bare };
 return { Kind::Channel, peerToChannel(peer).bare };
}
QString TypedId(Entry entry) {
 const auto prefix = (entry.kind == Kind::User) ? u"user:"_q
  : (entry.kind == Kind::Chat) ? u"chat:"_q : u"channel:"_q;
 return prefix + QString::number(qulonglong(entry.id));
}
bool Selectable(not_null<Main::Session*> session, not_null<PeerData*> peer) {
 if (peer->id == session->userPeerId() || peer->name().isEmpty()) return false;
 if (const auto user = peer->asUser()) return !user->isInaccessible();
 if (const auto chat = peer->asChat()) return chat->amIn() && !chat->isMigrated();
 if (const auto channel = peer->asChannel()) return channel->amIn();
 return false;
}
template <typename Data>
Page ReadPage(not_null<Main::Session*> session, const Data &data, bool complete) {
 session->data().processUsers(data.vusers());
 session->data().processChats(data.vchats());
 auto page = Page{ {}, complete };
 for (const auto &dialog : data.vdialogs().v) {
  dialog.match([&](const MTPDdialog &value) {
   const auto id = peerFromMTP(value.vpeer());
   if (!id) return;
   const auto peer = session->data().peer(id);
   auto date = 0;
   for (const auto &message : data.vmessages().v) {
    if (IdFromMessage(message) == value.vtop_message().v
      && PeerFromMessage(message) == id) {
     date = DateFromMessage(message);
     break;
    }
   }
   page.dialogs.push_back({ ToEntry(id), value.vtop_message().v, date,
    peer->name().toStdString(), Selectable(session, peer) });
  }, [](const auto &) {}); // Folder/community pseudo-rows are not chats.
 }
 return page;
}
void UnlockAllowlistWindows(not_null<Main::Session*> session) {
 Core::App().enumerateWindows([=](not_null<Controller*> window) {
  if (window->maybeSession() == session) {
   crl::on_main(window, [=] { window->widget()->clearAllowlistLock(); });
  }
 });
}
} // namespace

AllowlistLockWidget::AllowlistLockWidget(
 QWidget *parent, not_null<Controller*> window)
: LockWidget(parent, window)
, _scroll(this, st::defaultSolidScroll)
, _layout(_scroll->setOwnedWidget(
 object_ptr<Ui::VerticalLayout>(_scroll.data())).data()) {
 _layout->add(object_ptr<Ui::FlatLabel>(_layout,
  u"Choose your chats"_q, st::allowlistTitle), st::allowlistRowPadding);
 _layout->add(object_ptr<Ui::FlatLabel>(_layout,
  u"Select the chats you want to use in Allowgram. Only selected chats will be shown. Your selection is saved on this device."_q,
  st::allowlistDescription), st::allowlistRowPadding);
 _search = _layout->add(object_ptr<Ui::InputField>(_layout, st::allowlistInput,
  Ui::InputField::Mode::SingleLine, rpl::single(u"Search your chats"_q)),
  st::allowlistRowPadding);
 _search->setObjectName(u"allowgramPickerSearch"_q);
 _search->changes() | rpl::on_next([=] { rebuildRows(); }, lifetime());
 _status = _layout->add(object_ptr<Ui::FlatLabel>(_layout, QString(),
  st::allowlistDescription), st::allowlistRowPadding);
 _rowsLayout = _layout->add(object_ptr<Ui::VerticalLayout>(_layout),
  st::allowlistRowPadding);
 _submit = Ui::CreateChild<Ui::RoundButton>(this,
  tr::lng_allowgram_save_continue(), st::allowlistSubmit);
 _submit->setObjectName(u"allowgramPickerSave"_q);
 _submit->setClickedCallback([=] { submit(); });
 _retry = Ui::CreateChild<Ui::RoundButton>(this,
  rpl::single(u"Reload chats"_q), st::defaultBoxButton);
 _retry->setObjectName(u"allowgramPickerReload"_q);
 _retry->setClickedCallback([=] { load(); });
 _logout = Ui::CreateChild<Ui::RoundButton>(this,
  tr::lng_settings_logout(), st::defaultBoxButton);
 _logout->setClickedCallback([=] { window->showLogoutConfirmation(); });
 _submit->show(); _retry->show(); _logout->show();
 _submit->setDisabled(true);
 window->account().sessionChanges() | rpl::on_next([=] {
  _api.reset(); _session = {}; _model.cancel();
  rebuildRows(); _submit->setDisabled(true);
  showError(u"Account changed. Please log in again."_q);
 }, lifetime());
 crl::on_main(this, [=] { load(); });
}
AllowlistLockWidget::~AllowlistLockWidget() = default;
bool AllowlistLockWidget::sameSession() const {
 const auto session = _session.get();
 return session && window()->maybeSession() == session
  && !session->allowlistConfigured();
}
void AllowlistLockWidget::load() {
 _api.reset();
 _generation = _model.start();
 const auto session = window()->maybeSession();
 _session = session ? base::make_weak(session) : base::weak_ptr<Main::Session>();
 rebuildRows(); updateStatus();
 if (!sameSession()) { failed(_generation); return; }
 _api = std::make_unique<MTP::Sender>(&_session->mtp());
 requestNext();
}
void AllowlistLockWidget::requestNext() {
 if (!sameSession() || !_api || _model.ready() || _model.failed()) return;
 const auto generation = _generation;
 const auto cursor = _model.cursor();
 const auto fail = [=] { failed(generation); };
 if (cursor.pinned) {
  _api->request(MTPmessages_GetPinnedDialogs(MTP_int(cursor.folder)))
   .done([=](const MTPmessages_PeerDialogs &result) {
    if (!sameSession() || generation != _generation) return;
    result.match([&](const MTPDmessages_peerDialogs &data) {
     receivePage(generation, ReadPage(_session.get(), data, true));
    });
   }).fail(fail).handleAllErrors().send();
 } else {
  const auto offset = cursor.peer.id
   ? _session->data().peer(ToPeer(cursor.peer))->input()
   : MTP_inputPeerEmpty();
  _api->request(MTPmessages_GetDialogs(
   MTP_flags(MTPmessages_GetDialogs::Flag::f_exclude_pinned
    | MTPmessages_GetDialogs::Flag::f_folder_id),
   MTP_int(cursor.folder), MTP_int(cursor.date), MTP_int(cursor.message),
   offset, MTP_int(100), MTP_long(0)))
   .done([=](const MTPmessages_Dialogs &result) {
    if (!sameSession() || generation != _generation) return;
    result.match([&](const MTPDmessages_dialogsNotModified &) {
     failed(generation);
    }, [&](const MTPDmessages_dialogs &data) {
     receivePage(generation, ReadPage(_session.get(), data, true));
    }, [&](const MTPDmessages_dialogsSlice &data) {
     receivePage(generation, ReadPage(_session.get(), data, false));
    });
   }).fail(fail).handleAllErrors().send();
 }
}
void AllowlistLockWidget::receivePage(std::uint64_t generation, Page page) {
 if (!sameSession() || generation != _generation) return;
 if (!_model.accept(generation, page)) { failed(generation); return; }
 updateStatus();
 if (_model.ready()) rebuildRows();
 else crl::on_main(this, [=] { requestNext(); });
}
void AllowlistLockWidget::failed(std::uint64_t generation) {
 if (generation != _generation) return;
 _model.fail(generation);
 updateStatus();
}
void AllowlistLockWidget::rebuildRows() {
 for (const auto row : _rows) delete row;
 _rows.clear();
 if (!_model.ready()) return;
 const auto query = _search->getLastText().trimmed();
 auto choices = std::vector<std::pair<QString, Entry>>();
 for (const auto &[id, title] : _model.candidates()) {
  const auto name = QString::fromStdString(title);
  if (query.isEmpty() || name.contains(query, Qt::CaseInsensitive)) {
   choices.emplace_back(name, id);
  }
 }
 std::sort(choices.begin(), choices.end(), [](const auto &a, const auto &b) {
  return QString::localeAwareCompare(a.first, b.first) < 0;
 });
 for (const auto &[name, id] : choices) {
  const auto kind = (id.kind == Kind::User) ? u"Chat"_q
   : (id.kind == Kind::Chat) ? u"Group"_q : u"Group / channel"_q;
  const auto row = _rowsLayout->add(object_ptr<Ui::Checkbox>(_rowsLayout,
   name + u"  ·  "_q + kind, _model.selected().contains(id)),
   st::allowlistRowPadding);
  row->setObjectName(u"allowgramPicker_"_q + TypedId(id));
  row->setAllowTextLines(2);
  row->setTextBreakEverywhere(true);
  row->checkedChanges() | rpl::on_next([=](bool checked) {
   if (!_model.choose(id, checked)) {
    row->setChecked(!checked, Ui::Checkbox::NotifyAboutChange::DontNotify);
    showError(u"Too many selected chats. Deselect a chat first."_q);
   } else updateStatus();
  }, row->lifetime());
  _rows.push_back(row);
 }
 _layout->resizeToWidth(std::min(width(), st::allowlistContentWidth));
}
void AllowlistLockWidget::updateStatus() {
 _submit->setDisabled(!_model.canSave() || !sameSession());
 _retry->setDisabled(!_model.ready() && !_model.failed());
 if (_model.failed()) {
  showError(u"Couldn't load all chats. Check your connection, then reload chats."_q);
 } else if (!_model.ready()) {
  _status->setText(u"Loading your chats, including pinned and archived chats…"_q);
 } else if (_model.candidates().empty()) {
  _status->setText(u"No available chats yet. Open or join a chat in Telegram, then reload here."_q);
 } else {
  _status->setText(u"%1 selected · %2 available chats"_q
   .arg(qlonglong(_model.selected().size())).arg(qlonglong(_model.candidates().size())));
 }
}
void AllowlistLockWidget::submit() {
 if (!sameSession() || !_model.canSave()) return;
 auto users = QStringList();
 auto groups = QStringList();
 for (const auto id : _model.selected()) {
  const auto peer = _session->data().peer(ToPeer(id));
  if (!Selectable(_session.get(), peer)) {
   showError(u"A selected chat is no longer available. Reload chats and select again."_q);
   return;
  }
  (id.kind == Kind::User ? users : groups).push_back(TypedId(id));
 }
 const auto error = _session->configureAllowlist(users.join('\n'), groups.join('\n'));
 if (!error.isEmpty()) { showError(error); return; }
 UnlockAllowlistWindows(_session.get());
}
void AllowlistLockWidget::showError(const QString &error) {
 _status->setText(error);
}
void AllowlistLockWidget::setInnerFocus() { _search->setFocus(); }
void AllowlistLockWidget::resizeEvent(QResizeEvent *e) {
 const auto w = std::max(1, std::min(width(), st::allowlistContentWidth));
 const auto left = (width() - w) / 2;
 const auto top = std::min(st::allowlistContentTop, 60);
 _submit->resizeToWidth(w);
 _retry->resizeToWidth(w / 2);
 _logout->resizeToWidth(w / 2);
 const auto footer = _submit->height() + std::max(_retry->height(), _logout->height()) + 32;
 const auto y = std::max(top, height() - footer);
 _submit->moveToLeft(left, y);
 _retry->moveToLeft(left, y + _submit->height() + 8);
 _logout->moveToLeft(left + w / 2, y + _submit->height() + 8);
 _scroll->setGeometry(left, top, w, std::max(y - top - 12, 0));
 _layout->resizeToWidth(w);
}
void AllowlistLockWidget::keyPressEvent(QKeyEvent *e) {
 if (e->key() == Qt::Key_Escape || e->key() == Qt::Key_Back) {
  e->accept(); return;
 }
 LockWidget::keyPressEvent(e);
}
} // namespace Window
