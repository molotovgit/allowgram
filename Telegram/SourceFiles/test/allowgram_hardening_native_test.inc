auto checks = QJsonArray();
auto failures = 0;
const auto check = [&](bool pass, const char *name) {
	checks.push_back(QJsonObject{ { "name", name }, { "pass", pass } });
	failures += pass ? 0 : 1;
	auto progress = QFile(qEnvironmentVariable("ALLOWGRAM_HARDENING_REPORT"));
	if (progress.open(QIODevice::WriteOnly)) {
		progress.write(QJsonDocument(QJsonObject{ { "checks", checks }, { "failures", failures }, { "finished", false } }).toJson());
	}
};
auto &domain = account().domain();
check(domain.accountsAuthedCount() == 0, "fresh profile has no authorized user");
const auto initialCount = domain.accounts().size();
check(domain.add(MTP::Environment::Production) == nullptr, "hidden Domain add callback denied before login");
domain.addActivated(MTP::Environment::Production);
check(domain.accounts().size() == initialCount, "hidden activated addition leaves initial login intact");
const auto makeUser = [](uint64 id, bool self) {
	return MTPUser(MTP_user(
		MTP_flags((self ? MTPDuser::Flag::f_self : MTPDuser::Flags())
			| MTPDuser::Flag::f_first_name),
		MTP_long(id), MTPlong(), MTP_string("Synthetic"),
		MTPstring(), MTPstring(), MTPstring(), MTPUserProfilePhoto(),
		MTPUserStatus(), MTPint(), MTPVector<MTPRestrictionReason>(),
		MTPstring(), MTPstring(), MTPEmojiStatus(), MTPVector<MTPUsername>(),
		MTPRecentStory(), MTPPeerColor(), MTPPeerColor(), MTPint(), MTPlong(),
		MTPlong(), MTPlong()));
};
auto placeholder = std::make_unique<Main::Account>(&domain, u"data"_q, 1);
const auto placeholderPtr = placeholder.get();
domain.local().startAdded(placeholderPtr, nullptr);
domain.accountAddedInStorage({ 1, std::move(placeholder) });
check(!placeholderPtr->sessionExists(), "synthetic stored placeholder remains unsigned in");
const auto firstCreated = account().createSession(makeUser(99, true));
check(firstCreated, "first user session creation remains possible");
if (firstCreated) {
	auto &session = account().session();
	check(domain.accountsAuthedCount() == 1, "first synthetic user exists");
	check(!placeholderPtr->createSession(makeUser(100, true)), "hidden second user-session creation denied");
	check(!placeholderPtr->sessionExists(), "rejected login leaves placeholder unchanged");
	const auto previousActive = &domain.active();
	domain.activate(placeholderPtr);
	check(&domain.active() == previousActive, "unsigned placeholder cannot replace an authenticated user");
	check(domain.add(MTP::Environment::Test) == nullptr, "test-environment additional account denied");
	domain.addActivated(MTP::Environment::Production, true);
	check(domain.accountsAuthedCount() == 1, "new-window addition cannot authorize a second user");
	check(session.configureAllowlist(u"42\n99"_q, u"chat:42"_q).isEmpty(), "isolated synthetic allow-list saved");
	const auto user = session.data().processUser(makeUser(42, false));
	const auto group = session.data().peer(peerFromChat(ChatId(42)));
	check(session.allowlistAllows(user->id), "allowed DM navigation policy retained");
	check(!session.allowlistAllows(session.userPeerId()), "explicitly allowlisted self-chat still denied");
	const auto selfHistory = session.data().history(session.user());
	const auto previousSelfMessage = selfHistory->lastMessage();
	auto selfSend = Api::MessageToSend(Api::SendAction(selfHistory));
	selfSend.textWithTags = { u"retained self-chat callback"_q, {} };
	session.api().sendMessage(std::move(selfSend));
	check(selfHistory->lastMessage() == previousSelfMessage, "direct self-chat API creates no local message");
	check(!session.canPresentPeerProfile(user->id), "allowlisted user profile denied");
	check(session.canPresentPeerProfile(group->id), "allowed group information retained");
	check(!Info::Memento(user).allowlistAllows(&session), "cached user-info navigation memento denied");
	check(!Info::Profile::Memento(user, PeerId()).allowlistAllows(&session), "direct profile memento denied");
	check(Info::Memento(group).allowlistAllows(&session), "group-info memento positive control");
	check(!Info::Memento(session.user()).allowlistAllows(&session), "self-chat info memento denied");
	check(AllowgramSendTextAllowed(TextWithTags{ u"123 # * O'zbekiston \u040E\u0437\u0431\u0435\u043A \u4F60\u597D"_q, {} }), "real semantic multilingual text positive");
	check(!AllowgramSendTextAllowed(TextWithTags{ u"pasted \U0001F600"_q, {} }), "real semantic pasted emoji denied");
	check(!AllowgramSendTextAllowed(TextWithEntities{ u"x"_q, { EntityInText(EntityType::CustomEmoji, 0, 1, u"123"_q) } }), "real semantic custom emoji denied");
	check(!AllowgramSendTextAllowed(TextWithEntities{ u"x"_q, { EntityInText(EntityType::Pre, 0, 1, u"\U0001F600"_q) } }), "semantic entity-owned emoji denied");
	auto file = Ui::PreparedFile(QString());
	file.information = std::make_unique<Ui::PreparedFileInformation>();
	file.information->filemime = u"application/pdf"_q;
	file.content = "%PDF-1.7 ordinary";
	check(AllowgramSendFileAllowed(file), "real prepared ordinary document positive");
	file.content = "GIF89a renamed document";
	check(!AllowgramSendFileAllowed(file), "real prepared renamed GIF denied");
	file.content = "%PDF-1.7 ordinary";
	file.caption.text = u"caption \U0001F600"_q;
	check(!AllowgramSendFileAllowed(file), "real prepared document emoji caption denied");
	const auto document = session.data().processDocument(MTP_document(MTP_flags(0),
		MTP_long(123), MTP_long(1), MTP_bytes("synthetic-reference"), MTP_int(1),
		MTP_string("application/pdf"), MTP_long(20), MTPVector<MTPPhotoSize>(),
		MTPVector<MTPVideoSize>(), MTP_int(2), MTP_vector<MTPDocumentAttribute>({
			MTP_documentAttributeFilename(MTP_string("report.pdf")) })));
	const auto media = document->createMediaView();
	media->setBytes("%PDF-1.7 ordinary");
	check(AllowgramSendDocumentAllowed(document), "actual Data document ingestion and bytes allow ordinary file");
	media->setBytes("GIF89a renamed report.pdf");
	check(!AllowgramSendDocumentAllowed(document), "actual Session resolver blocks cached renamed GIF");
	media->setBytes(QByteArray());
	check(!AllowgramSendDocumentAllowed(document), "actual Session resolver denies missing cached bytes");
	const auto navigation = controller().sessionController();
	check(navigation != nullptr, "production session controller constructed");
	if (navigation) {
		auto profile = Info::Memento(user);
		check(!profile.createWidget(bodyWidget(), navigation, Window::Column::Third, QRect()), "direct right-column user-info factory denied");
		check(!profile.createWidget(bodyWidget(), navigation, Window::Column::Second, QRect()), "direct main-column user-info factory denied");
		const auto oldStatus = session.user()->emojiStatusId();
		session.data().emojiStatuses().set(EmojiStatusId{ .documentId = 123 });
		check(session.user()->emojiStatusId() == oldStatus, "hidden emoji-status setter does not mutate account metadata");
		session.user()->setEmojiStatus(oldStatus);
		auto menu = std::make_unique<Window::MainMenu>(bodyWidget(), navigation);
		auto menuNames = QStringList();
		for (const auto widget : menu->findChildren<QWidget*>()) {
			if (const auto button = dynamic_cast<Ui::SettingsButton*>(widget)) {
				menuNames.push_back(button->accessibilityName());
			}
		}
		check(menuNames.contains(u"Settings"_q), "actual main menu retains Settings positive control");
		check(!menuNames.contains(u"Saved Messages"_q), "actual main menu has no Saved Messages action");
		check(!menuNames.contains(u"Add Account"_q), "actual main menu has no Add Account action");
		menu.reset();
		auto pixels = QImage(32, 32, QImage::Format_RGB32);
		pixels.fill(Qt::blue);
		auto imageBytes = QByteArray();
		auto buffer = QBuffer(&imageBytes);
		check(buffer.open(QIODevice::WriteOnly) && pixels.save(&buffer, "JPEG"), "synthetic ordinary photo encoded");
		const auto photoData = MTP_photo(MTP_flags(0), MTP_long(500), MTP_long(1),
			MTP_bytes("synthetic-photo"), MTP_int(1700000000),
			MTP_vector<MTPPhotoSize>({ MTP_photoCachedSize(MTP_string("x"), MTP_int(32), MTP_int(32), MTP_bytes(imageBytes)) }),
			MTPVector<MTPVideoSize>(), MTP_int(2));
		const auto incoming = MTP_message(
			MTP_flags(MTPDmessage::Flag::f_from_id | MTPDmessage::Flag::f_media),
			MTP_int(17), MTP_peerUser(MTP_long(42)), MTPint(), MTPstring(),
			MTP_peerUser(MTP_long(42)), MTPPeer(), MTPMessageFwdHeader(),
			MTPlong(), MTPlong(), MTPPeer(), MTPMessageReplyHeader(),
			MTP_int(1700000000), MTP_string(u"selected \U0001F600 ordinary selected quote"_q),
			MTP_messageMediaPhoto(MTP_flags(MTPDmessageMediaPhoto::Flag::f_photo), photoData, MTPint(), MTPDocument()),
			MTPReplyMarkup(), MTPVector<MTPMessageEntity>(), MTPint(), MTPint(),
			MTPMessageReplies(), MTPint(), MTPstring(), MTPlong(), MTPMessageReactions(),
			MTPVector<MTPRestrictionReason>(), MTPint(), MTPint(), MTPlong(),
			MTPFactCheck(), MTPint(), MTPlong(), MTPSuggestedPost(), MTPint(), MTPstring(), MTPRichMessage());
		const auto item = session.data().addNewMessage(incoming, {}, NewMessageType::Existing);
		check(item && item->media() && item->media()->photo(), "ordinary photo message ingested by production model");
		const auto history = session.data().history(user);
		history->clearFolder();
		history->addOlderSlice({ incoming });
		history->addOlderSlice({});
		history->addNewerSlice({});
		check(history->loadedAtTop() && history->loadedAtBottom(), "synthetic conversation is fully loaded before compose navigation");
		if (item && item->media() && item->media()->photo()) {
			const auto photo = item->media()->photo();
			auto viewer = Media::View::OverlayWidget();
			viewer.show(Media::View::OpenRequest(navigation, photo, user));
			check(viewer.isHidden(), "direct allowlisted user profile-photo request denied");
			viewer.show(Media::View::OpenRequest(navigation, photo, nullptr, MsgId(), PeerId()));
			check(viewer.isHidden(), "context-free photo request denied");
			viewer.show(Media::View::OpenRequest(navigation, session.data().photo(501), item, MsgId(), PeerId()));
			check(viewer.isHidden(), "unrelated profile photo cannot borrow an allowed message context");
			viewer.hide();
			viewer.show(Media::View::OpenRequest(navigation, photo, item, MsgId(), PeerId()));
			check(!viewer.isHidden(), "ordinary received message photo opens in real native viewer");
			viewer.close();
		}
		auto picker = ChatHelpers::TabbedMemento();
		check(!navigation->allowlistAllowsSection(&picker), "cached picker section navigation denied");
		check(!picker.createWidget(bodyWidget(), navigation, Window::Column::Third, QRect()), "direct picker widget factory denied");
		navigation->showPeerHistory(user);
		check(navigation->activeChatCurrent().history() == session.data().history(user), "allowed DM actually navigates");
		navigation->showPeerHistory(session.user());
		check(navigation->activeChatCurrent().history() == session.data().history(user), "retained self-chat callback cannot navigate");
		navigation->showPeerInfo(user);
		check(!Info::Memento(user).allowlistAllows(&session), "profile callback remains centrally denied");
	}
	placeholderPtr->setSessionUserId(UserId(100));
	placeholderPtr->createSession(UserId(100), QByteArray(), QDataStream::Qt_5_1,
		std::make_unique<Main::SessionSettings>());
	check(placeholderPtr->sessionExists() && domain.accountsAuthedCount() == 2, "legacy stored session restoration preserved");
	check(session.userId() == UserId(99), "legacy restoration does not erase first account");
	check(domain.add(MTP::Environment::Production) == nullptr, "additional account denied with legacy multi-account state");
}
auto report = QFile(qEnvironmentVariable("ALLOWGRAM_HARDENING_REPORT"));
if (report.open(QIODevice::WriteOnly)) {
	report.write(QJsonDocument(QJsonObject{
		{ "checks", checks }, { "failures", failures },
		{ "synthetic", true }, { "networkDisabled", true },
		{ "finished", true },
	}).toJson());
	report.close();
}
QTimer::singleShot(1800, this, [] { Core::Quit(); });
