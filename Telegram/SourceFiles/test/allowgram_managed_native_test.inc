// Runs inside the disposable hardening fixture, after its existing checks.
// Real Session/storage/UI + real managed client; ONLY HTTP and MTProto transport
// are offline fixtures. These checks do not prove an authenticated Telegram login.
{
 const auto managedCheck = [&](const QString &name, bool passed) {
  const auto label = name.toUtf8(); check(passed, label.constData());
 };
 const auto vectors = AllowgramManagedFixture::vectors();
 managedCheck(QStringLiteral("managed fixture has account-bound signed vectors"),
  vectors[QStringLiteral("subject")].toString() == QStringLiteral("99"));
 const auto sessionBefore = account().maybeSession();
 if (sessionBefore && vectors[QStringLiteral("subject")].toString() == QStringLiteral("99")) {
  const auto navigation = controller().sessionController();
  auto &managed = sessionBefore->managedAllowlist();
  const auto waitUntil = [&](const std::function<bool()> &ready) {
   auto timer = QElapsedTimer(); timer.start();
   while (!ready() && timer.elapsed() < 5000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QThread::msleep(1);
   }
   return ready();
  };
  // Let ordinary composer assertions finish before intentionally revoking chats.
  auto settle = QEventLoop(); QTimer::singleShot(750, &settle, &QEventLoop::quit); settle.exec();
  managedCheck(QStringLiteral("managed test begins with the original picker policy"),
   sessionBefore->allowlistConfigured() && !managed.paired()
   && sessionBefore->allowlistAllows(peerFromUser(UserId(42))));
  QCoreApplication::instance()->setProperty("managedFixtureAckCount", 0);
  managedCheck(QStringLiteral("managed invitation starts explicit enrollment"),
   managed.pair(vectors[QStringLiteral("invitation")].toString()));
  managedCheck(QStringLiteral("managed enrollment completes apply and ACK"), waitUntil([&] {
   return !managed.busy() && QCoreApplication::instance()->property("managedFixtureAckCount").toInt() == 1;
  }));
  managedCheck(QStringLiteral("managed revision 2 is persisted and acknowledged"),
   managed.paired() && !sessionBefore->settings().managedAllowlist().isEmpty()
   && QCoreApplication::instance()->property("managedFixtureAckRevision").toInt() == 2);
  managedCheck(QStringLiteral("managed enrollment preserves the current Session object"), account().maybeSession() == sessionBefore);
  managedCheck(QStringLiteral("managed local picker cannot replace the head policy"),
   !sessionBefore->configureAllowlist(QStringLiteral("43"), QString()).isEmpty());
  const auto stored = sessionBefore->settings().serialize();
  auto restored = Main::SessionSettings(); restored.addFromSerialized(stored); restored.validateManagedAllowlist(UserId(99));
  managedCheck(QStringLiteral("managed session settings roundtrip verifies signed peers"),
   restored.allowlistConfigured() && restored.managedAllowlist() == sessionBefore->settings().managedAllowlist()
   && restored.allowlistPeers().contains(peerFromUser(UserId(42))));
  auto foreign = Main::SessionSettings(); foreign.addFromSerialized(stored); foreign.validateManagedAllowlist(UserId(100));
  managedCheck(QStringLiteral("managed cache cannot move to a different account"),
   foreign.allowlistConfigured() && foreign.allowlistPeers().empty());
  auto damagedBytes = stored; if (!damagedBytes.isEmpty()) damagedBytes[damagedBytes.size()-1] = char(damagedBytes.back() ^ 1);
  auto damaged = Main::SessionSettings(); damaged.addFromSerialized(damagedBytes); damaged.validateManagedAllowlist(UserId(99));
  managedCheck(QStringLiteral("damaged managed cache is configured deny-all, not local onboarding"),
   damaged.allowlistConfigured() && damaged.allowlistPeers().empty());
  navigation->showPeerHistory(peerFromUser(UserId(42)));
  managedCheck(QStringLiteral("managed revocation precondition has an active allowed chat"),
   waitUntil([&] { return navigation->activeChatCurrent().history() == sessionBefore->data().history(peerFromUser(UserId(42))); }));
  managed.refresh();
  managedCheck(QStringLiteral("managed deny-all policy is applied and acknowledged"), waitUntil([&] {
   return !managed.busy() && QCoreApplication::instance()->property("managedFixtureAckRevision").toInt() == 3;
  }));
  managedCheck(QStringLiteral("managed deny-all keeps the same configured session"),
   account().maybeSession() == sessionBefore && sessionBefore->allowlistConfigured()
   && sessionBefore->allowlistPeers().empty() && !sessionBefore->allowlistAllows(peerFromUser(UserId(42))));
  managedCheck(QStringLiteral("managed revoke clears the actual active chat"), !navigation->activeChatCurrent().history());
  auto emptyRestored = Main::SessionSettings(); emptyRestored.addFromSerialized(sessionBefore->settings().serialize()); emptyRestored.validateManagedAllowlist(UserId(99));
  managedCheck(QStringLiteral("managed empty policy remains deny-all after settings reload"),
   emptyRestored.allowlistConfigured() && emptyRestored.allowlistPeers().empty());
  managed.refresh();
  managedCheck(QStringLiteral("managed higher revision can grant new chats without logout"), waitUntil([&] {
   return !managed.busy() && QCoreApplication::instance()->property("managedFixtureAckRevision").toInt() == 4;
  }) && account().maybeSession() == sessionBefore && sessionBefore->allowlistAllows(peerFromUser(UserId(43)))
   && sessionBefore->allowlistAllows(peerFromChat(ChatId(43))) && !sessionBefore->allowlistAllows(peerFromUser(UserId(42))));
  managed.refresh();
  managedCheck(QStringLiteral("managed rollback neither changes live peers nor sends an ACK"),
   waitUntil([&] { return !managed.busy(); }) && sessionBefore->allowlistAllows(peerFromUser(UserId(43)))
   && !sessionBefore->allowlistAllows(peerFromUser(UserId(42)))
   && QCoreApplication::instance()->property("managedFixtureAckCount").toInt() == 3);
  managed.stop();
 }
}
