if (qEnvironmentVariableIsSet("ALLOWGRAM_MANDATORY_RUNTIME")) {
const auto mandatoryApplication = QCoreApplication::instance();
const auto mandatorySyntheticReplies
	= mandatoryApplication->property("allowgramSyntheticCallReplies");
mandatoryApplication->setProperty("allowgramSyntheticCallReplies", true);
const auto mandatoryRpcCount = [&](mtpTypeId type, bool allowed = true) {
	const auto name = QByteArray("allowgramRpc_") + QByteArray::number(type)
		+ (allowed ? "_allowed" : "_denied");
	return mandatoryApplication->property(name.constData()).toInt();
};
const auto mandatoryDrainCalls = [] {
	for (auto i = 0; i != 12; ++i) {
		QCoreApplication::sendPostedEvents();
		QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
	}
};
auto mandatoryDraftsMaterialized = 0;
auto mandatoryDraftLifetime = rpl::lifetime();
Core::App().materializeLocalDraftsRequests(
) | rpl::on_next([&] {
	++mandatoryDraftsMaterialized;
}, mandatoryDraftLifetime);
const auto mandatoryTargetVersion = Core::RunningUpdateVersion() + 1;
const auto nowForMandatoryUpdate = base::unixtime::now();
auto mandatoryStateError = QString();
auto mandatoryState = Core::Updates::MandatoryUpdateState();
mandatoryState.active = true;
mandatoryState.firstSeen = nowForMandatoryUpdate - 600;
mandatoryState.deadline = nowForMandatoryUpdate - 1;
mandatoryState.target.tag = u"mandatory-native-fixture"_q;
mandatoryState.target.fileName = u"allowgram-update-stable-win-x64-7.2.8.9.tdup"_q;
mandatoryState.target.sha256 = QByteArray(64, 'b');
mandatoryState.target.size = 64;
mandatoryState.target.packedVersion = mandatoryTargetVersion;
mandatoryState.target.displayVersion = Core::Updates::DisplayUpdateVersion(
	mandatoryTargetVersion);
check(Core::Updates::WriteMandatoryUpdateState(
	cWorkingDir(),
	mandatoryState,
	&mandatoryStateError), "production mandatory update state can be seeded in fixture profile");
const auto beforeMandatoryRequest = mandatoryRpcCount(mtpc_phone_requestCall);
Core::App().calls().startOutgoingCall(user, { .isConfirmed = true });
mandatoryDrainCalls();
check(Core::App().calls().currentCall()
	&& mandatoryRpcCount(mtpc_phone_requestCall) == beforeMandatoryRequest + 1,
	"positive control call exists before mandatory update apply");
const auto beforeMandatoryDiscard = mandatoryRpcCount(mtpc_phone_discardCall);
const auto mandatoryWindow = Core::App().activePrimaryWindow();
const auto mandatoryActiveBefore = navigation
	? navigation->activeChatCurrent().history()
	: nullptr;
Core::UpdateChecker().applyMandatoryUpdateNow();
mandatoryDrainCalls();
const auto appliedMandatoryState = Core::Updates::ReadMandatoryUpdateState(
	cWorkingDir(),
	&mandatoryStateError);
check(appliedMandatoryState && appliedMandatoryState->applyStarted,
	"mandatory update apply marks persisted state");
check(mandatoryDraftsMaterialized > 0,
	"mandatory update apply materializes local drafts");
check(Core::MandatoryUpdateBlocksUse(),
	"mandatory update apply blocks normal session use");
check(!session.allowlistAllows(user->id),
	"mandatory update gate overrides allowed DM policy");
check(!session.canCallPeer(user->id),
	"mandatory update gate overrides allowed call policy");
check(!Core::App().calls().currentCall(),
	"mandatory update apply tears down the active call");
check(mandatoryRpcCount(mtpc_phone_discardCall) == beforeMandatoryDiscard + 1,
	"mandatory update apply performs one bound active-call cleanup");
const auto mandatoryLockActive = [&] {
	return Core::UpdateChecker().mandatoryUpdateLocked()
		&& mandatoryWindow
		&& mandatoryWindow->isLayerShown();
};
const auto ensureMandatoryLock = [&](const char *name) {
	if (!mandatoryLockActive()) {
		Core::UpdateChecker().applyMandatoryUpdateNow();
		mandatoryDrainCalls();
	}
	check(mandatoryLockActive(), name);
};
check(Core::UpdateChecker().mandatoryUpdateLocked(),
	"mandatory update apply shows updater-only recovery lock");
check(mandatoryLockActive(),
	"mandatory update lock covers the existing app window");
if (navigation) {
	navigation->showPeerHistory(group,
		{ anim::type::instant, anim::activation::background }, ShowAtTheEndMsgId);
	mandatoryDrainCalls();
	check(navigation->activeChatCurrent().history() == mandatoryActiveBefore,
		"mandatory update lock blocks existing-window chat navigation");
} else {
	check(false, "mandatory update lock test has a session controller");
}
if (mandatoryWindow) {
	mandatoryWindow->hideLayer(anim::type::instant);
	mandatoryDrainCalls();
	check(mandatoryLockActive(),
		"mandatory update blocker survives controller hideLayer close attempt");
	ensureMandatoryLock(
		"mandatory update blocker is active before settings-layer close route");
	mandatoryWindow->hideSettingsAndLayer(anim::type::instant);
	mandatoryDrainCalls();
	check(mandatoryLockActive(),
		"mandatory update blocker survives controller settings-layer close attempt");
	if (navigation) {
		ensureMandatoryLock(
			"mandatory update blocker is active before session uiShow hide route");
		navigation->uiShow()->hideLayer(anim::type::instant);
		mandatoryDrainCalls();
		check(mandatoryLockActive(),
			"mandatory update blocker survives session uiShow hide route");
		ensureMandatoryLock(
			"mandatory update blocker is active before ordinary replacement route");
		mandatoryWindow->show(
			Ui::MakeInformBox(TextWithEntities{
				u"ordinary replacement fixture"_q }),
			Ui::LayerOption::CloseOther,
			anim::type::instant);
		mandatoryDrainCalls();
		check(mandatoryLockActive(),
			"mandatory update blocker rejects ordinary CloseOther replacement");
	}
	ensureMandatoryLock(
		"mandatory update blocker is active before direct MainWindow close route");
	mandatoryWindow->widget()->ui_hideSettingsAndLayer(anim::type::instant);
	mandatoryDrainCalls();
	check(mandatoryLockActive(),
		"mandatory update blocker survives direct MainWindow close route");
	const auto fixtureOpenPath = cWorkingDir()
		+ u"allowgram-mandatory-open.txt"_q;
	auto fixtureOpenWritten = false;
	{
		auto fixture = QFile(fixtureOpenPath);
		fixtureOpenWritten = fixture.open(QIODevice::WriteOnly)
			&& (fixture.write("owned fixture") == 13);
	}
	check(fixtureOpenWritten, "fixture-owned file-open input prepared");
	ensureMandatoryLock(
		"mandatory update blocker is active before file-open dispatch route");
	mandatoryWindow->widget()->handleStartFiles(
		QStringList(),
		QStringList{ fixtureOpenPath });
	mandatoryDrainCalls();
	check(mandatoryLockActive(),
		"mandatory update blocker survives direct file-open dispatch route");
}
const auto afterMandatoryRequest = mandatoryRpcCount(mtpc_phone_requestCall);
Core::App().calls().startOutgoingCall(user, { .isConfirmed = true });
mandatoryDrainCalls();
check(mandatoryRpcCount(mtpc_phone_requestCall) == afterMandatoryRequest
	&& !Core::App().calls().currentCall(),
	"mandatory update gate blocks fresh calls after apply starts");
mandatoryApplication->setProperty(
	"allowgramSyntheticCallReplies",
	mandatorySyntheticReplies);
}
