auto checks = std::make_shared<QJsonArray>();
auto failures = std::make_shared<int>(0);
auto readyForFinalConfirmation = std::make_shared<bool>(false);
const auto cascadeReportPath = qEnvironmentVariable(
	"ALLOWGRAM_OPTIONAL_RESTART_CASCADE_REPORT");
const auto launchReportPath = qEnvironmentVariable(
	"ALLOWGRAM_OPTIONAL_RESTART_LAUNCH_REPORT");
const auto packagePath = qEnvironmentVariable(
	"ALLOWGRAM_OPTIONAL_RESTART_PACKAGE");
auto writeCascadeReport = [=](bool finished) {
	auto report = QFile(cascadeReportPath);
	if (report.open(QIODevice::WriteOnly)) {
		report.write(QJsonDocument(QJsonObject{
			{ "checks", *checks },
			{ "failures", *failures },
			{ "finished", finished },
			{ "readyForFinalConfirmation", *readyForFinalConfirmation },
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
	writeCascadeReport(false);
};
auto checkPlain = [=](bool pass, const char *name) {
	check(pass, name, QString());
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
		"synthetic account session created for transfer cascade");
}
if (!account().sessionExists()) {
	writeCascadeReport(true);
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
auto packageCopyIndex = std::make_shared<int>(0);
auto makeDocument = [=](const QString &name, int size) {
	const auto id = uint64(900000 + ++*serial);
	return sessionPtr->data().processDocument(MTP_document(MTP_flags(0),
		MTP_long(id), MTP_long(1), MTP_bytes("restart-cascade"), MTP_int(1),
		MTP_string("application/pdf"), MTP_long(size), MTPVector<MTPPhotoSize>(),
		MTPVector<MTPVideoSize>(), MTP_int(2), MTP_vector<MTPDocumentAttribute>({
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
	prepared->filename = u"restart-upload.pdf"_q;
	prepared->filemime = u"application/pdf"_q;
	prepared->content = "%PDF-1.7 synthetic upload";
	prepared->filesize = prepared->content.size();
	prepared->document = MTP_document(MTP_flags(0),
		MTP_long(uint64(920000 + ++*serial)), MTP_long(1),
		MTP_bytes("restart-upload"), MTP_int(1),
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
	const auto document = makeDocument(u"restart-download.pdf"_q, 4096);
	const auto item = manager.generateExternalItem(document);
	manager.addLoadingExternal(
		{ item, document },
		QDir(cWorkingDir()).filePath(u"restart-download.pdf"_q),
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
auto launchReportExists = [=] {
	return !launchReportPath.isEmpty() && QFileInfo(launchReportPath).isFile();
};
auto removeStagedManifest = [=] {
	return QFile::remove(QDir(cWorkingDir()).filePath(
		u"tupdates/temp/tdata/stage-manifest.bin"_q));
};
auto ensureReady = [=] {
	if (packagePath.isEmpty()) {
		return false;
	}
	const auto copiedPath = QDir(cWorkingDir()).filePath(
		u"tupdates/fixture-download-%1.tdup"_q.arg(++*packageCopyIndex));
	QFile::remove(copiedPath);
	if (!QDir().mkpath(QFileInfo(copiedPath).absolutePath())
		|| !QFile::copy(packagePath, copiedPath)
		|| !Core::TestUnpackUpdateForOptionalRestartFixture(copiedPath)) {
		return false;
	}
	Core::UpdateChecker().test();
	return Core::checkReadyUpdate();
};
checkPlain(ensureReady(),
	"signed fixture package unpacks and passes ready authentication");
checkPlain(startUpload(), "real paused uploader reports an in-progress upload");
checkPlain(startDownload(), "real download manager reports an in-progress download");
checkPlain(Core::RestartToUpdate(),
	"ready-button restart enters normal transfer confirmation cascade");
checkPlain(!Core::Quitting() && !cRestartingUpdate() && !launchReportExists(),
	"update restart is unarmed while the first confirmation is pending");
auto step = std::make_shared<Fn<void(int)>>();
*step = [=](int index) mutable {
	auto &manager = Core::App().downloadManager();
	switch (index) {
	case 0: {
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
		checkPlain(!Core::Quitting() && !cRestartingUpdate() && !launchReportExists(),
			"download prompt keeps update restart unarmed after upload stop");
		check(triggerBoxButton(1),
			"cancelling the second confirmation uses the production Cancel button",
			visibleButtonNames().join(u"|"_q));
		QTimer::singleShot(900, this, [=] { (*step)(2); });
		break;
	}
	case 2: {
		checkPlain(!Core::Quitting() && !cRestartingUpdate() && !launchReportExists(),
			"cancelled download confirmation leaves the app running");
		checkPlain(manager.loadingInProgress(),
			"cancelled download confirmation preserves the download");
		checkPlain(Core::RestartToUpdate(),
			"retry with a pending download re-enters confirmation");
		checkPlain(removeStagedManifest(),
			"fixture removes the authenticated stage while confirmation is pending");
		QTimer::singleShot(900, this, [=] {
			check(triggerBoxButton(0),
				"accepting with a removed stage uses the production Stop button",
				visibleButtonNames().join(u"|"_q));
			QTimer::singleShot(900, this, [=] { (*step)(3); });
		});
		break;
	}
	case 3: {
		checkPlain(!Core::Quitting() && !cRestartingUpdate() && !launchReportExists(),
			"removed staged manifest prevents update launch after confirmation");
		checkPlain(ensureReady(),
			"signed fixture package can be restaged after fail-closed invalidation");
		checkPlain(startUpload(),
			"second real paused upload starts for accepted cascade");
		checkPlain(startDownload(),
			"second real download starts for accepted cascade");
		checkPlain(Core::RestartToUpdate(),
			"retry with valid stage enters the accepted cascade");
		QTimer::singleShot(900, this, [=] { (*step)(4); });
		break;
	}
	case 4: {
		check(triggerBoxButton(0),
			"accepting second upload confirmation uses the production Stop button",
			visibleButtonNames().join(u"|"_q));
		QTimer::singleShot(900, this, [=] { (*step)(5); });
		break;
	}
	case 5: {
		checkPlain(manager.loadingInProgress(),
			"second download remains pending until its own confirmation");
		checkPlain(!Core::Quitting() && !cRestartingUpdate() && !launchReportExists(),
			"final download confirmation still gates the update launch");
		*readyForFinalConfirmation = true;
		writeCascadeReport(false);
		const auto accepted = triggerBoxButton(0);
		check(accepted,
			"accepting final download confirmation permits update restart",
			visibleButtonNames().join(u"|"_q));
		writeCascadeReport(accepted && *failures == 0);
		QTimer::singleShot(1200, this, [=] {
			if (!accepted || *failures != 0) {
				writeCascadeReport(false);
				QCoreApplication::quit();
			}
		});
		break;
	}
	}
};
QTimer::singleShot(900, this, [=] { (*step)(0); });