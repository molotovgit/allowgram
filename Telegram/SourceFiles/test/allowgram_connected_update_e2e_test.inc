auto checks = std::make_shared<QJsonArray>();
auto failures = std::make_shared<int>(0);
auto updateReady = std::make_shared<bool>(false);
auto updateButtonClicked = std::make_shared<bool>(false);
auto finalConfirmationAccepted = std::make_shared<bool>(false);
const auto reportPath = qEnvironmentVariable("ALLOWGRAM_CONNECTED_E2E_REPORT");
auto writeReport = [=](bool finished) {
	auto report = QFile(reportPath);
	if (report.open(QIODevice::WriteOnly)) {
		report.write(QJsonDocument(QJsonObject{
			{ "checks", *checks },
			{ "failures", *failures },
			{ "finished", finished },
			{ "updateReady", *updateReady },
			{ "updateButtonClicked", *updateButtonClicked },
			{ "finalConfirmationAccepted", *finalConfirmationAccepted },
			{ "readyStageHash", Core::ReadyUpdateStageHash() },
			{ "runningUpdateVersion",
				QString::number(Core::RunningUpdateVersion()) },
			{ "buildAllowgramSequence",
				double(Core::BuildAllowgramSequence) },
			{ "appVersion", double(AppVersion) },
			{ "appVersionStr", QString::fromLatin1(AppVersionStr) },
			{ "checkerState", double(int(Core::UpdateChecker().state())) },
			{ "quitting", Core::Quitting() },
			{ "restartingUpdate", cRestartingUpdate() },
			{ "buttonVisible", QCoreApplication::instance()->property(
				"allowgramConnectedUpdateButtonVisible").toBool() },
			{ "buttonClickedProperty", QCoreApplication::instance()->property(
				"allowgramConnectedUpdateButtonClicked").toBool() },
		}).toJson());
	}
};
auto check = [=](bool pass, const char *name, const QString &detail) {
	auto item = QJsonObject{
		{ "name", name },
		{ "pass", pass },
	};
	if (!detail.isEmpty()) {
		item.insert("detail", detail);
	}
	checks->push_back(item);
	*failures += pass ? 0 : 1;
	writeReport(false);
};
auto checkPlain = [=](bool pass, const char *name) {
	check(pass, name, QString());
};
const auto failAndQuit = [=](const char *name, const QString &detail) {
	check(false, name, detail);
	writeReport(true);
	QCoreApplication::quit();
};
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
if (!account().sessionExists()) {
	checkPlain(account().createSession(makeUser(99, true)),
		"synthetic account session created for connected updater e2e");
}
if (!account().sessionExists()) {
	writeReport(true);
	QCoreApplication::quit();
	return;
}
auto &session = account().session();
const auto sessionPtr = not_null(&session);
const auto user = session.data().processUser(makeUser(42, false));
const auto peerId = user->id;
checkPlain(session.configureAllowlist(u"42\n99"_q, QString()).isEmpty(),
	"synthetic account allowlist accepts the transfer peer");
clearAllowlistLock();
const auto navigation = controller().sessionController();
checkPlain(navigation != nullptr,
	"production session controller constructed for confirmation boxes");
checkPlain(Core::App().activePrimaryWindow() != nullptr,
	"active primary window exists for transfer confirmation boxes");
const auto history = session.data().history(peerId);
history->clearFolder();
history->addOlderSlice({});
history->addNewerSlice({});
const auto serial = std::make_shared<int>(0);
auto makeDocument = [=](const QString &name, int size) {
	const auto id = uint64(900000 + ++*serial);
	return sessionPtr->data().processDocument(MTP_document(MTP_flags(0),
		MTP_long(id), MTP_long(1), MTP_bytes("connected-update"),
		MTP_int(1), MTP_string("application/pdf"), MTP_long(size),
		MTPVector<MTPPhotoSize>(), MTPVector<MTPVideoSize>(), MTP_int(2),
		MTP_vector<MTPDocumentAttribute>({
			MTP_documentAttributeFilename(MTP_string(name)) })));
};
auto startUpload = [=] {
	const auto uploadId = FullMsgId(
		peerId,
		sessionPtr->data().nextLocalMessageId());
	auto prepared = MakePreparedFile({
		.id = uint64(910000 + ++*serial),
		.type = SendMediaType::File,
		.to = FileLoadTo(peerId, Api::SendOptions(), {}, MsgId()),
	});
	prepared->filename = u"connected-upload.pdf"_q;
	prepared->filemime = u"application/pdf"_q;
	prepared->content = "%PDF-1.7 synthetic connected upload";
	prepared->filesize = prepared->content.size();
	prepared->document = MTP_document(MTP_flags(0),
		MTP_long(uint64(920000 + ++*serial)), MTP_long(1),
		MTP_bytes("connected-upload"), MTP_int(1),
		MTP_string("application/pdf"), MTP_long(prepared->filesize),
		MTPVector<MTPPhotoSize>(), MTPVector<MTPVideoSize>(), MTP_int(2),
		MTP_vector<MTPDocumentAttribute>({
			MTP_documentAttributeFilename(MTP_string(prepared->filename)) }));
	sessionPtr->uploader().pause(uploadId);
	sessionPtr->uploader().upload(uploadId, prepared);
	return sessionPtr->uploadsInProgress();
};
auto startDownload = [=] {
	auto &manager = Core::App().downloadManager();
	const auto document = makeDocument(u"connected-download.pdf"_q, 4096);
	const auto item = manager.generateExternalItem(document);
	manager.addLoadingExternal(
		{ item, document },
		QDir(cWorkingDir()).filePath(u"connected-download.pdf"_q),
		4096,
		[] {});
	return manager.loadingInProgress();
};
auto visibleButtonNames = [=] {
	auto result = QStringList();
	for (const auto widget : QApplication::allWidgets()) {
		if (const auto box = dynamic_cast<Ui::BoxLayerWidget*>(widget)) {
			if (!box->isHidden()) {
				result.push_back(u"box"_q);
			}
		}
		if (const auto button = dynamic_cast<Ui::RoundButton*>(widget)) {
			if (button->isVisible()) {
				result.push_back(button->accessibilityName());
			}
		}
	}
	return result;
};
auto triggerBoxButton = [=](int index) {
	for (const auto widget : QApplication::allWidgets()) {
		if (const auto box = dynamic_cast<Ui::BoxLayerWidget*>(widget)) {
			if (!box->isHidden()) {
				box->triggerButton(index);
				return true;
			}
		}
	}
	return false;
};
auto step = std::make_shared<Fn<void(int)>>();
auto waitForButtonClick = std::make_shared<Fn<void(int)>>();
*step = [=](int index) mutable {
	auto &manager = Core::App().downloadManager();
	switch (index) {
	case 0: {
		checkPlain(!Core::Quitting() && !cRestartingUpdate(),
			"update restart is unarmed while upload confirmation is pending");
		check(triggerBoxButton(0),
			"accepting upload confirmation uses the production Stop button",
			visibleButtonNames().join(u"|"_q));
		QTimer::singleShot(900, this, [=] { (*step)(1); });
		break;
	}
	case 1: {
		checkPlain(!sessionPtr->uploadsInProgress(),
			"upload confirmation stops only upload activity");
		checkPlain(manager.loadingInProgress(),
			"download remains active after upload confirmation");
		checkPlain(!Core::Quitting() && !cRestartingUpdate(),
			"download prompt keeps update restart unarmed after upload stop");
		*finalConfirmationAccepted = triggerBoxButton(0);
		check(*finalConfirmationAccepted,
			"accepting final download confirmation permits update restart",
			visibleButtonNames().join(u"|"_q));
		writeReport(*finalConfirmationAccepted && *failures == 0);
		QTimer::singleShot(1800, this, [=] {
			if (!*finalConfirmationAccepted || *failures != 0) {
				writeReport(false);
				QCoreApplication::quit();
			}
		});
		break;
	}
	}
};
*waitForButtonClick = [=](int attempt) mutable {
	const auto clicked = QCoreApplication::instance()->property(
		"allowgramConnectedUpdateButtonClicked").toBool();
	if (clicked) {
		*updateButtonClicked = true;
		checkPlain(true,
			"dialogs bottom update button performed the restart callback");
		QTimer::singleShot(900, this, [=] { (*step)(0); });
		return;
	}
	if (attempt >= 120) {
		failAndQuit(
			"dialogs bottom update button performed the restart callback",
			visibleButtonNames().join(u"|"_q));
		return;
	}
	QTimer::singleShot(250, this, [=] {
		(*waitForButtonClick)(attempt + 1);
	});
};
auto waitForReady = std::make_shared<Fn<void(int)>>();
*waitForReady = [=](int attempt) mutable {
	if (Core::UpdateChecker().state() == Core::UpdateChecker::State::Ready
		&& Core::checkReadyUpdate()) {
		*updateReady = true;
		checkPlain(true,
			"signed update downloaded, staged and passes ready authentication");
		checkPlain(startUpload(),
			"real paused uploader reports an in-progress upload");
		checkPlain(startDownload(),
			"real download manager reports an in-progress download");
		QCoreApplication::instance()->setProperty(
			"allowgramConnectedUpdateClickPermit",
			true);
		(*waitForButtonClick)(0);
		return;
	}
	if (attempt >= 180) {
		failAndQuit(
			"signed update downloaded, staged and passes ready authentication",
			QString::number(int(Core::UpdateChecker().state())));
		return;
	}
	QTimer::singleShot(250, this, [=] { (*waitForReady)(attempt + 1); });
};
QCoreApplication::instance()->setProperty(
	"allowgramConnectedUpdateClickPermit",
	false);
QCoreApplication::instance()->setProperty(
	"allowgramConnectedUpdateButtonClicked",
	false);
Core::UpdateChecker().test();
QTimer::singleShot(250, this, [=] { (*waitForReady)(0); });
