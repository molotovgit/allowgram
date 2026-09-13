const auto application = QCoreApplication::instance();
application->setProperty("allowgramSyntheticCallReplies", true);
application->setProperty("allowgramNextCallId", QVariant::fromValue<qulonglong>(900));
const auto originalSettings = session.settings().serialize();
const auto drainCalls = [] {
	for (auto i = 0; i != 12; ++i) {
		QCoreApplication::sendPostedEvents();
		QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
	}
};
const auto rpcCount = [&](mtpTypeId type, bool allowed = true) {
	const auto name = QByteArray("allowgramRpc_") + QByteArray::number(type)
		+ (allowed ? "_allowed" : "_denied");
	return application->property(name.constData()).toInt();
};
const auto effects = [&] {
	auto result = 0;
	for (const auto name : { "allowgramCallPermissions", "allowgramCallPresentation",
		"allowgramCallRing", "allowgramCallDeviceSetup", "allowgramCallVideoSetup",
		"allowgramCallControllerStart", "allowgramCallSignalingReceived", "allowgramCallSound", "allowgramCallErrorPresentation" }) {
		result += application->property(name).toInt();
	}
	return result;
};
const auto setAllowed = [&](bool allowed) {
	auto &peers = const_cast<base::flat_set<PeerId>&>(session.settings().allowlistPeers());
	if (allowed) {
		peers.insert(user->id);
	} else {
		peers.erase(user->id);
	}
	session.settings().addFromSerialized(session.settings().serialize());
};
auto &testPeers = const_cast<base::flat_set<PeerId>&>(session.settings().allowlistPeers());
testPeers.insert(peerFromUser(UserId(44)));
testPeers.insert(peerFromUser(UserId(45)));
session.settings().addFromSerialized(session.settings().serialize());
const auto denied = session.data().processUser(makeUser(43, false));
const auto bot = session.data().processUser(makeUser(44, false, true));
const auto unresolved = session.data().user(UserId(45));
check(session.allowlistAllows(bot->id) && !session.canCallPeer(bot->id), "explicitly allowed bot still cannot call");
check(session.allowlistAllows(unresolved->id) && !session.canCallPeer(unresolved->id), "explicitly allowed unresolved user cannot call");
check(!session.canCallPeer(group->id), "allowed group cannot be a private call recipient");
for (const auto target : { denied, bot, unresolved, session.user() }) {
	for (const auto video : { false, true }) {
		const auto before = effects();
		const auto requests = rpcCount(mtpc_phone_requestCall);
		Core::App().calls().startOutgoingCall(target, { .video = video, .isConfirmed = true });
		check(effects() == before && !Core::App().calls().currentCall(), "denied hidden voice/video route has zero presentation ring device effects");
		check(rpcCount(mtpc_phone_requestCall) == requests, "denied hidden route never reaches approved request boundary");
	}
}
const auto protocol = MTP_phoneCallProtocol(MTP_flags(0), MTP_int(65), MTP_int(100),
	MTP_vector<MTPstring>({ MTP_string("synthetic") }));
const auto requested = [&](uint64 id, uint64 hash, uint64 admin, uint64 participant, int date, int hashSize = 32) {
	return MTPPhoneCall(MTP_phoneCallRequested(MTP_flags(0), MTP_long(id), MTP_long(hash),
		MTP_int(date), MTP_long(admin), MTP_long(participant), MTP_bytes(QByteArray(hashSize, 'a')), protocol));
};
const auto update = [&](const MTPPhoneCall &call) {
	Core::App().calls().handleUpdate(&session, MTP_updatePhoneCall(call));
};
for (const auto caller : { 0ULL, 43ULL, 44ULL, 45ULL, 99ULL, 999ULL }) {
	const auto before = effects();
	update(requested(100, 200, caller, 99, base::unixtime::now()));
	check(effects() == before && !Core::App().calls().currentCall(), "denied incoming caller has zero model presentation ring device signaling effects");
}
for (const auto &invalid : { requested(101, 201, 42, 100, base::unixtime::now()),
	requested(0, 201, 42, 99, base::unixtime::now()), requested(101, 0, 42, 99, base::unixtime::now()),
	requested(101, 201, 42, 99, 1), requested(101, 201, 42, 99, base::unixtime::now(), 3) }) {
	const auto before = effects();
	update(invalid);
	check(effects() == before && !Core::App().calls().currentCall(), "malformed foreign or stale incoming request has zero effects");
}
application->setProperty("allowgramGroupStart", 0);
Core::App().calls().startOrJoinGroupCall(navigation->uiShow(), group, {});
check(application->property("allowgramGroupStart").toInt() == 0, "hidden group call initiation stops before join UI");
application->setProperty("allowgramConferenceStart", 0);
Core::App().calls().startOrJoinConferenceCall({ .show = navigation->uiShow() });
check(application->property("allowgramConferenceStart").toInt() == 0, "hidden conference initiation stops before model allocation");
application->setProperty("allowgramRtmpStart", 0);
Core::App().calls().showStartWithRtmp(navigation->uiShow(), group);
check(application->property("allowgramRtmpStart").toInt() == 0, "hidden RTMP call initiation stops before stream UI");
const auto accepted = [&](uint64 id, uint64 hash) {
	return MTPPhoneCall(MTP_phoneCallAccepted(MTP_flags(0), MTP_long(id), MTP_long(hash),
		MTP_int(base::unixtime::now()), MTP_long(99), MTP_long(42), MTP_bytes(QByteArray(256, 'a')), protocol));
};
const auto confirmed = [&](uint64 id, uint64 hash, uint64 admin, uint64 participant) {
	return MTPPhoneCall(MTP_phoneCall(MTP_flags(0), MTP_long(id), MTP_long(hash), MTP_int(base::unixtime::now()),
		MTP_long(admin), MTP_long(participant), MTP_bytes(QByteArray(256, 'a')), MTP_long(1), protocol,
		MTP_vector<MTPPhoneConnection>({}), MTP_int(base::unixtime::now()), MTPDataJSON()));
};
for (const auto video : { false, true }) {
	const auto before = rpcCount(mtpc_phone_requestCall);
	Core::App().calls().startOutgoingCall(user, { .video = video, .isConfirmed = true });
	check(Core::App().calls().currentCall() && rpcCount(mtpc_phone_requestCall) == before + 1,
		"allowed voice/video creates actual Call and reaches approved serialized request boundary");
	drainCalls();
	const auto call = Core::App().calls().currentCall();
	check(call && call->id() && call->state() == Calls::Call::State::Waiting,
		"actual outgoing request callback binds identity and enters waiting state");
	if (call) {
		const auto id = call->id();
		const auto beforeConfirm = rpcCount(mtpc_phone_confirmCall);
		check(!call->handleUpdate(accepted(id, id + 101)), "mismatched update hash cannot confirm actual call");
		update(accepted(id, id + 100));
		drainCalls();
		check(call->state() == Calls::Call::State::Established && rpcCount(mtpc_phone_confirmCall) == beforeConfirm + 1,
			"actual outgoing confirmation reaches intercepted controller and active signaling boundary");
		const auto signal = application->property("allowgramCallSignalingReceived").toInt();
		Core::App().calls().handleUpdate(&session, MTP_updatePhoneCallSignalingData(MTP_long(id + 1), MTP_bytes("synthetic")));
		check(application->property("allowgramCallSignalingReceived").toInt() == signal, "wrong call ID update cannot reach signaling effect");
		Core::App().calls().handleUpdate(&session, MTP_updatePhoneCallSignalingData(MTP_long(id), MTP_bytes("synthetic")));
		check(application->property("allowgramCallSignalingReceived").toInt() == signal + 1, "actual active call admits its matching signaling update");
		const auto discarded = rpcCount(mtpc_phone_discardCall);
		call->hangup();
		check(rpcCount(mtpc_phone_discardCall) == discarded + 1, "actual associated hangup passes transport authorization");
		drainCalls();
		check(!Core::App().calls().currentCall(), "actual hangup removes call object and permission context");
	}
}
Core::App().calls().startOutgoingCall(user, { .isConfirmed = true });
drainCalls();
if (const auto call = Core::App().calls().currentCall()) {
	const auto old = call->id();
	update(MTP_phoneCallDiscarded(MTP_flags(MTPDphoneCallDiscarded::Flag::f_reason),
		MTP_long(old), MTP_phoneCallDiscardReasonBusy(), MTPint()));
	check(call->state() == Calls::Call::State::Busy, "actual busy update reaches redial state");
	call->redial();
	drainCalls();
	check(call->id() != old && call->state() == Calls::Call::State::Waiting, "actual redial obtains a fresh call identity");
	check(!call->handleUpdate(accepted(old, old + 100)), "old call update cannot attach to actual redial");
	call->hangup();
	drainCalls();
}
const auto incomingId = uint64(2001);
const auto incomingHash = uint64(2101);
const auto beforeReceived = rpcCount(mtpc_phone_receivedCall);
update(requested(incomingId, incomingHash, 42, 99, base::unixtime::now()));
drainCalls();
if (const auto call = Core::App().calls().currentCall()) {
	check(call->state() == Calls::Call::State::WaitingIncoming && rpcCount(mtpc_phone_receivedCall) == beforeReceived + 1,
		"eligible incoming request creates actual pending call and approved received request");
	const auto beforeAccept = rpcCount(mtpc_phone_acceptCall);
	call->answer();
	drainCalls();
	check(call->state() == Calls::Call::State::ExchangingKeys && rpcCount(mtpc_phone_acceptCall) == beforeAccept + 1,
		"actual incoming answer reaches approved acceptance request boundary");
	application->setProperty("allowgramDeferSignaling", true);
	update(confirmed(incomingId, incomingHash, 42, 99));
	check(call->state() == Calls::Call::State::Established, "actual incoming confirmation reaches intercepted active controller boundary");
	const auto cancelled = application->property("allowgramSignalCancelled").toInt();
	check(application->property("allowgramPendingSignal").toInt() != 0, "synthetic signaling request remains queued before revocation");
	const auto stop = application->property("allowgramCallControllerStop").toInt();
	const auto discard = rpcCount(mtpc_phone_discardCall);
	setAllowed(false);
	check(call->state() != Calls::Call::State::Established && application->property("allowgramCallControllerStop").toInt() > stop,
		"active allowlist revocation synchronously stops controller state");
	check(rpcCount(mtpc_phone_discardCall) == discard + 1, "revoked actual call retains narrowly bound cleanup");
	check(application->property("allowgramSignalCancelled").toInt() == cancelled + 1, "revocation synchronously cancels queued signaling request");
	application->setProperty("allowgramDeferSignaling", false);
	const auto before = effects();
	call->answer();
	call->redial();
	check(!call->handleSignalingData(MTP_updatePhoneCallSignalingData(MTP_long(incomingId), MTP_bytes("synthetic")).c_updatePhoneCallSignalingData()),
		"revoked actual call denies incoming signaling");
	check(effects() == before, "revoked hidden answer redial and signaling produce zero new presentation ring device effects");
	drainCalls();
	check(!Core::App().calls().currentCall(), "revoked actual call finishes and releases its context");
	setAllowed(true);
} else {
	check(false, "eligible incoming call exists for acceptance and revocation tests");
}
for (const auto incoming : { false, true }) {
	const auto discard = rpcCount(mtpc_phone_discardCall);
	if (incoming) {
		update(requested(2002, 2102, 42, 99, base::unixtime::now()));
	} else {
		Core::App().calls().startOutgoingCall(user, { .isConfirmed = true });
	}
	check(Core::App().calls().currentCall() != nullptr, "pending call exists before revocation");
	setAllowed(false);
	drainCalls();
	check(!Core::App().calls().currentCall(), "pending revocation and late callback cannot retain a call");
	check(rpcCount(mtpc_phone_discardCall) == discard + 1, "pending revocation cleans only its validated late or incoming call ID");
	setAllowed(true);
}
Core::App().calls().startOutgoingCall(user, { .isConfirmed = true });
drainCalls();
if (const auto call = Core::App().calls().currentCall()) {
	const auto before = application->property("allowgramConferenceStart").toInt();
	update(MTP_phoneCallDiscarded(MTP_flags(MTPDphoneCallDiscarded::Flag::f_reason), MTP_long(call->id()),
		MTP_phoneCallDiscardReasonMigrateConferenceCall(MTP_string("synthetic")), MTPint()));
	drainCalls();
	check(!Core::App().calls().currentCall() && application->property("allowgramConferenceStart").toInt() == before,
		"private call migration update cannot create or join a conference");
}
session.settings().addFromSerialized(originalSettings);
application->setProperty("allowgramSyntheticCallReplies", false);
check(session.canCallPeer(user->id), "original synthetic allowlist restored after call lifecycle checks");