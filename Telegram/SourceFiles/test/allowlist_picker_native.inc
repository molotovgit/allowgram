// Executed only in the offline fixture, inside the real picker widget.
struct PickerReport { QJsonArray checks; int failures=0; };
const auto report=std::make_shared<PickerReport>();
const auto path=qEnvironmentVariable("ALLOWGRAM_PICKER_REPORT");
const auto check=[report,path](bool pass,const char *name) {
 report->checks.push_back(QJsonObject{{"name",name},{"pass",pass}});
 report->failures += pass ? 0 : 1;
 QFile file(path);
 if (file.open(QIODevice::WriteOnly)) file.write(QJsonDocument(QJsonObject{
  {"checks",report->checks},{"failures",report->failures},{"finished",false},
  {"syntheticOffline",true}}).toJson());
};
const auto wait=[&](Fn<bool()> ready) {
 for (int i=0;i!=500 && !ready();++i) {
  QEventLoop loop; QTimer::singleShot(10,&loop,&QEventLoop::quit); loop.exec();
 }
 return ready();
};
const auto app=QCoreApplication::instance();
check(sameSession(),"picker belongs to fresh synthetic logged-in account");
check(!QFile::exists(QDir(cWorkingDir()).filePath(u"allowgram-sheet-access.json"_q)),"no sheet access profile exists");
check(wait([&]{return _model.failed();}),"RPC failure leaves picker locked with retry");
check(!_session->allowlistConfigured() && _submit->isDisabled(),"RPC failure cannot configure policy");
app->setProperty("allowgramPickerScene","notmodified"); load();
check(wait([&]{return _model.failed();}),"unexpected not-modified response fails closed");
app->setProperty("allowgramPickerScene","empty"); load();
check(wait([&]{return _model.ready();}),"empty main and archive lists finish loading");
check(_rows.empty() && !_model.canSave() && _submit->isDisabled(),"empty selection cannot unlock app");
const auto stale=_generation;
app->setProperty("allowgramPickerScene","normal"); load();
check(wait([&]{return _model.ready();}),"real serialized pinned/archive/paged responses reach ready");
check(app->property("allowgramPickerCursorCorrect").toBool(),"cursor matches both peer and message ID, not colliding message IDs");
check(_model.candidates().size()==7 && _rows.size()==7,"all seven available current chats are deduplicated");
check(!_model.candidates().contains({Kind::User,99}),"Saved Messages is excluded");
check(!_model.candidates().contains({Kind::Chat,66}),"forbidden chat is excluded");
check(_model.candidates().contains({Kind::Channel,81}) && _model.candidates().contains({Kind::Chat,7}),"archive and archived pinned chats are included");
check(_model.selected().empty(),"no chats are preselected");
const auto oldSize=_model.candidates().size();
receivePage(stale,Page{{{Entry{Kind::User,888},1,1,"stale",true}},true});
check(_model.candidates().size()==oldSize,"old generation cannot inject stale chats");
const auto select=[&](const QString &name) {
 for (const auto row:_rows) if(row->objectName()==name) {row->setChecked(true); return true;}
 return false;
};
check(select(u"allowgramPicker_user:42"_q),"real user checkbox can be selected");
check(select(u"allowgramPicker_chat:42"_q),"real basic-group checkbox can be selected");
check(select(u"allowgramPicker_channel:81"_q),"real channel checkbox can be selected");
check(_model.selected().size()==3 && !_submit->isDisabled(),"save is enabled only for a complete nonempty selection");
_search->setTextWithTags({u"Alice"_q,{}});
check(_rows.size()==1 && _model.selected().size()==3,"search hides rows without losing their selection");
_search->setTextWithTags({QString(),{}});
check(_rows.size()==7 && _model.selected().size()==3,"clearing search restores the checked rows");
app->setProperty("allowgramPickerSaveFail",true); submit();
check(!_session->allowlistConfigured() && sameSession(),"injected disk-write failure does not configure or unlock");
app->setProperty("allowgramPickerSaveFail",false);
check(_submit->geometry().bottom()<=height() && _scroll->geometry().bottom()<_submit->y(),"save button stays visible outside the scrolling list");
check(_logout->isVisible(),"own Log Out remains available during selection");
setProperty("pickerCaptureReady",true);
const auto root=this->window()->widget();
const auto session=_session.get();
const auto hold=qEnvironmentVariableIsSet("ALLOWGRAM_PICKER_CAPTURE") ? 75000 : 0;
const auto guarded=QPointer<AllowlistLockWidget>(this);
if (hold) QTimer::singleShot(5000,app,[=] {
 check(bool(guarded),"capture: picker survives first five seconds");
});
QTimer::singleShot(hold,app,[=] {
 if (hold) check(bool(guarded),"capture: picker survives full hold");
 if (!guarded) {
  QFile file(path);
  if (file.open(QIODevice::WriteOnly)) file.write(QJsonDocument(QJsonObject{
   {"checks",report->checks},{"failures",report->failures},{"finished",true},
   {"syntheticOffline",true},{"captureWidgetDestroyed",true}}).toJson());
  QCoreApplication::exit(1); return;
 }
 guarded->submit();
 check(session->allowlistConfigured(),"real verified disk save configures selection");
 check(session->allowlistPeers().size()==3,"saved policy contains exactly the three selected peers");
 check(session->allowlistAllows(peerFromUser(UserId(42)))
  && session->allowlistAllows(peerFromChat(ChatId(42)))
  && session->allowlistAllows(peerFromChannel(ChannelId(81))),"selected typed peers are accessible");
 check(!session->allowlistAllows(peerFromUser(UserId(43)))
  && !session->allowlistAllows(peerFromUser(UserId(44)))
  && !session->allowlistAllows(peerFromChannel(ChannelId(42)))
  && !session->allowlistAllows(peerFromChat(ChatId(7))),"all unselected peers remain denied");
 auto restored=Main::SessionSettings();
 restored.addFromSerialized(session->settings().serialize());
 check(restored.allowlistConfigured() && restored.allowlistPeers()==session->allowlistPeers(),"persisted-settings serialization restores the exact selection");
 check(!session->configureAllowlist(u"43"_q,QString()).isEmpty()
  && !session->allowlistAllows(peerFromUser(UserId(43))),"existing saved policy cannot be silently overwritten");
 QTimer::singleShot(100,root,[=] {
  QFile file(path);
  if (file.open(QIODevice::WriteOnly)) file.write(QJsonDocument(QJsonObject{
   {"checks",report->checks},{"failures",report->failures},{"finished",true},
   {"syntheticOffline",true},{"verifiedDiskSave",session->allowlistConfigured()}}).toJson());
  QCoreApplication::exit(report->failures?1:0);
 });
});
