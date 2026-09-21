// Synthetic serialized MTProto replies AFTER the real request guard. Network off.
const auto app = QCoreApplication::instance();
const auto offset = details::SerializedRequest::kMessageBodyPosition;
const auto type = request->size() > offset ? mtpTypeId((*request)[offset]) : mtpTypeId(0);
const auto allowed = !_requestFilter || _requestFilter(request);
if (allowed && (type == mtpc_messages_getPinnedDialogs || type == mtpc_messages_getDialogs || type == mtpc_messages_getPeerDialogs)) {
 request->requestId = requestId;
 storeRequest(requestId, request, std::move(callbacks));
 auto response = Response{ .requestId = requestId };
 const auto scene = app->property("allowgramPickerScene").toString();
 auto from = request->constData()+offset+1;
 const auto end = request->constData()+request->size();
 const auto number=[&] { if (from==end) Unexpected("Truncated fixture request"); return int(*from++); };
 int folder=0,date=0;
 if (type == mtpc_messages_getPinnedDialogs) {
  folder=number();
 } else if (type == mtpc_messages_getDialogs) {
  const auto flags=number();
  folder=(flags & 2) ? number() : 0;
  date=number(); const auto id=number();
  auto peer=MTPInputPeer();
  if (!peer.read(from,end)) Unexpected("Bad fixture offset peer");
  if (date) app->setProperty("allowgramPickerCursorCorrect",
   date==90 && id==10 && peer.type()==mtpc_inputPeerChannel
   && peer.c_inputPeerChannel().vchannel_id().v==42);
 }
 if (scene == "error") {
  MTP_rpc_error(MTP_int(400), MTP_string("PICKER_FIXTURE_FAILURE")).write(response.reply);
 } else if (scene == "notmodified" && type == mtpc_messages_getDialogs) {
  MTPmessages_Dialogs(MTP_messages_dialogsNotModified(MTP_int(0))).write(response.reply);
 } else {
  const auto makeUser = [](uint64 id, const char *name, bool self=false) {
   return MTPUser(MTP_user(
    MTP_flags((self ? MTPDuser::Flag::f_self : MTPDuser::Flags())
     | MTPDuser::Flag::f_first_name | MTPDuser::Flag::f_access_hash),
    MTP_long(id), MTP_long(id+1000), MTP_string(name),
    MTPstring(), MTPstring(), MTPstring(), MTPUserProfilePhoto(),
    MTPUserStatus(), MTPint(), MTPVector<MTPRestrictionReason>(),
    MTPstring(), MTPstring(), MTPEmojiStatus(), MTPVector<MTPUsername>(),
    MTPRecentStory(), MTPPeerColor(), MTPPeerColor(), MTPint(), MTPlong(),
    MTPlong(), MTPlong()));
  };
  const auto makeChat = [](uint64 id,const char *name) {
   return MTPChat(MTP_chat(MTP_flags(MTPDchat::Flag::f_creator), MTP_long(id),
    MTP_string(name), MTP_chatPhotoEmpty(), MTP_int(2), MTP_int(1700000000),
    MTP_int(1), MTPInputChannel(), MTPChatAdminRights(), MTPChatBannedRights()));
  };
  const auto makeChannel = [](uint64 id,const char *name) {
   auto wire=mtpBuffer{ mtpc_channel, (1U<<13)|(1U<<8), 0 };
   MTP_long(id).write(wire); MTP_long(id+1000).write(wire);
   MTP_string(name).write(wire); MTPChatPhoto(MTP_chatPhotoEmpty()).write(wire);
   MTP_int(1700000000).write(wire);
   auto result=MTPChat(); auto p=wire.constData();
   if (!result.read(p,p+wire.size())) Unexpected("Bad channel fixture");
   return result;
  };
  const auto notify=MTP_peerNotifySettings(MTP_flags(0),MTPBool(),MTPBool(),MTPint(),
   MTPNotificationSound(),MTPNotificationSound(),MTPNotificationSound(),MTPBool(),MTPBool(),
   MTPNotificationSound(),MTPNotificationSound(),MTPNotificationSound());
  auto dialogs=QVector<MTPDialog>();
  auto messages=QVector<MTPMessage>();
  auto users=QVector<MTPUser>{makeUser(99,"Synthetic picker account",true),
   makeUser(42,"Alice - synthetic"),makeUser(43,"Bob - synthetic"),makeUser(44,"Carol - synthetic")};
  auto chats=QVector<MTPChat>{makeChat(42,"Project team - synthetic"),makeChat(7,"Archived project - synthetic"),
   makeChannel(42,"News - synthetic"),makeChannel(81,"Announcements - synthetic"),
   MTP_chatForbidden(MTP_long(66),MTP_string("Forbidden - synthetic"))};
  const auto add=[&](MTPPeer peer,int when) {
   dialogs.push_back(MTP_dialog(MTP_flags(0),peer,MTP_int(10),MTP_int(0),MTP_int(0),
    MTP_int(0),MTP_int(0),MTP_int(0),MTP_int(0),notify,MTPint(),MTPDraftMessage(),MTPint(),MTPint()));
   auto wire=mtpBuffer{mtpc_message,0,0};
   MTP_int(10).write(wire); peer.write(wire); MTP_int(when).write(wire);
   MTP_string("Synthetic offline preview").write(wire);
   auto message=MTPMessage(); auto p=wire.constData();
   if (!message.read(p,p+wire.size())) Unexpected("Bad message fixture");
   messages.push_back(message);
  };
  const auto pinned=type==mtpc_messages_getPinnedDialogs;
  const auto peerDialogs=type==mtpc_messages_getPeerDialogs;
  if (scene != "empty") {
   if (peerDialogs) {add(MTP_peerUser(MTP_long(42)),120);add(MTP_peerUser(MTP_long(43)),110);add(MTP_peerChat(MTP_long(42)),100);add(MTP_peerChannel(MTP_long(42)),90);add(MTP_peerChannel(MTP_long(81)),80);}
   else if (!folder && pinned) {add(MTP_peerUser(MTP_long(42)),120);add(MTP_peerUser(MTP_long(99)),119);add(MTP_peerChat(MTP_long(66)),118);}
   else if (!folder && !date) {add(MTP_peerUser(MTP_long(42)),120);add(MTP_peerUser(MTP_long(43)),110);add(MTP_peerChat(MTP_long(42)),100);add(MTP_peerChannel(MTP_long(42)),90);}
   else if (!folder) {add(MTP_peerUser(MTP_long(44)),80);}
   else if (pinned) {add(MTP_peerChat(MTP_long(7)),70);add(MTP_peerChannel(MTP_long(42)),90);}
   else {add(MTP_peerChannel(MTP_long(81)),60);}
  }
  const auto ds=MTP_vector<MTPDialog>(dialogs);
  const auto ms=MTP_vector<MTPMessage>(messages);
  const auto cs=MTP_vector<MTPChat>(chats);
  const auto us=MTP_vector<MTPUser>(users);
  if (pinned || peerDialogs) MTPmessages_PeerDialogs(MTP_messages_peerDialogs(ds,ms,cs,us,
   MTP_updates_state(MTP_int(1),MTP_int(1),MTP_int(1700000000),MTP_int(1),MTP_int(0)))).write(response.reply);
  else if (!folder && !date && scene != "empty")
   MTPmessages_Dialogs(MTP_messages_dialogsSlice(MTP_int(7),ds,ms,cs,us)).write(response.reply);
  else MTPmessages_Dialogs(MTP_messages_dialogs(ds,ms,cs,us)).write(response.reply);
 }
 crl::on_main(_instance,[=,response=std::move(response)] {processCallback(response);});
 return;
}
