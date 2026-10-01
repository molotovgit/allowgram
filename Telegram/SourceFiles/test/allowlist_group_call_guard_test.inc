// Included inside the serialized native guard suite. No network or account data.
{
	auto eligible = true;
	const auto resolver = Fn<bool(uint64, uint64)>([&](uint64 id, uint64 hash) {
		return eligible && id == 1 && hash == 1;
	});
	auto context = MTP::AllowlistGroupCallContext(UserId(99), resolver);
	auto foreign = MTP::AllowlistGroupCallContext(UserId(99), resolver);
	const auto call = MTP_inputGroupCall(MTP_long(1), MTP_long(1));
	const auto other = MTP_inputGroupCall(MTP_long(2), MTP_long(1));
	const auto wrongHash = MTP_inputGroupCall(MTP_long(1), MTP_long(2));
	const auto join = [&](const MTPInputGroupCall &c, const MTPInputPeer &as) {
		return MTPphone_JoinGroupCall(MTP_flags(0), c, as, MTPstring(),
			MTPint256(), MTPbytes(), MTP_dataJSON(MTP_string("{}")));
	};
	const auto checkGroup = [&](const char *name, const Request &packet,
			bool expected, MTP::AllowlistGroupCallContext &proof, UserId self = UserId(99)) {
		++checks;
		if (MTP::AllowlistRequestAllowed(packet, self, allows,
				nullptr, nullptr, nullptr, &proof) != expected) {
			++failures;
			std::cerr << "FAIL: " << name << '\n';
		}
	};
	checkGroup("allowed group banner read", Request::Serialize(
		MTPphone_GetGroupCall(call, MTP_int(10))), true, context);
	checkGroup("unknown group read denied", Request::Serialize(
		MTPphone_GetGroupCall(other, MTP_int(10))), false, context);
	checkGroup("wrong group hash denied", Request::Serialize(
		MTPphone_GetGroupCall(wrongHash, MTP_int(10))), false, context);
	checkGroup("join requires owned lifecycle", Request::Serialize(
		join(call, MTP_inputPeerSelf())), false, context);
	checkGroup("unowned leave denied", Request::Serialize(
		MTPphone_LeaveGroupCall(call, MTP_int(100))), false, context);
	checkProof(!context.begin(2, 1), "unbound group ownership rejected");
	checkProof(context.begin(1, 1), "existing allowed group ownership bound");
	const auto joined = Request::Serialize(join(call, MTP_inputPeerSelf()));
	checkGroup("self group join admitted", joined, true, context);
	checkGroup("foreign session cannot borrow join", joined, false, foreign);
	checkGroup("wrong self account denied", joined, false, context, UserId(98));
	checkGroup("unauthenticated join denied", joined, false, context, UserId());
	checkGroup("explicit self join admitted", Request::Serialize(join(call,
		MTP_inputPeerUser(MTP_long(99), MTP_long(1)))), true, context);
	for (const auto &as : { MTPInputPeer(MTP_inputPeerChat(MTP_long(42))),
		MTPInputPeer(MTP_inputPeerChannel(MTP_long(42), MTP_long(1))),
		MTPInputPeer(MTP_inputPeerUser(MTP_long(42), MTP_long(1))),
		MTPInputPeer(MTP_inputPeerUserFromMessage(MTP_inputPeerSelf(), MTP_int(1), MTP_long(99))) }) {
		checkGroup("nonself/contextual join identity denied", Request::Serialize(join(call, as)), false, context);
	}
	checkGroup("wrapper retains group proof", Packet(MTP_int(mtpc_invokeWithLayer),
		MTP_int(222), join(call, MTP_inputPeerSelf())), true, context);
	checkGroup("wrapper retains group hash check", Packet(MTP_int(mtpc_invokeWithoutUpdates),
		join(wrongHash, MTP_inputPeerSelf())), false, context);
	checkGroup("trailing group payload denied", Packet(join(call, MTP_inputPeerSelf()),
		MTP_int(0)), false, context);
	checkGroup("truncated group payload denied", Packet(MTP_int(mtpc_phone_joinGroupCall),
		MTP_int(0), call), false, context);
	checkGroup("unknown join flags denied", Packet(MTP_int(mtpc_phone_joinGroupCall),
		MTP_int(1 << 25), call, MTP_inputPeerSelf(), MTP_dataJSON(MTP_string("{}"))), false, context);
	checkGroup("conference key-block join denied", Request::Serialize(MTPphone_JoinGroupCall(
		MTP_flags(MTPphone_JoinGroupCall::Flag::f_public_key | MTPphone_JoinGroupCall::Flag::f_block),
		call, MTP_inputPeerSelf(), MTPstring(), MTPint256(), MTP_bytes("x"),
		MTP_dataJSON(MTP_string("{}")))), false, context);
	const auto edit = [&](const MTPInputPeer &peer) {
		return MTPphone_EditGroupCallParticipant(MTP_flags(MTPphone_EditGroupCallParticipant::Flag::f_muted),
			call, peer, MTP_boolFalse(), MTPint(), MTPBool(), MTPBool(), MTPBool(), MTPBool());
	};
	checkGroup("self unmute admitted", Request::Serialize(edit(MTP_inputPeerSelf())), true, context);
	checkGroup("other participant moderation denied", Request::Serialize(edit(
		MTP_inputPeerUser(MTP_long(42), MTP_long(1)))), false, context);
	const auto activeRequests = std::vector<Request>{
		Request::Serialize(MTPphone_GetGroupParticipants(call, MTP_vector<MTPInputPeer>(),
			MTP_vector<MTPint>(), MTP_string(""), MTP_int(50))),
		Request::Serialize(MTPphone_CheckGroupCall(call, MTP_vector<MTPint>({ MTP_int(100) }))),
		Request::Serialize(MTPphone_JoinGroupCallPresentation(call, MTP_dataJSON(MTP_string("{}")))),
		Request::Serialize(MTPphone_GetGroupCallStreamChannels(call)),
		Request::Serialize(MTPupload_GetFile(MTP_flags(0), MTP_inputGroupCallStream(
			MTP_flags(0), call, MTP_long(0), MTP_int(0), MTPint(), MTPint()), MTP_long(0), MTP_int(131072))),
	};
	for (const auto &request : activeRequests) {
		checkGroup("bound group operation admitted", request, true, context);
	}
	for (const auto &request : std::vector<Request>{
		Request::Serialize(MTPphone_CreateGroupCall(MTP_flags(0), MTP_inputPeerChat(MTP_long(42)), MTP_int(1), MTPstring(), MTPint())),
		Request::Serialize(MTPphone_DiscardGroupCall(call)),
		Request::Serialize(MTPphone_StartScheduledGroupCall(call)),
		Request::Serialize(MTPphone_EditGroupCallTitle(call, MTP_string("no"))),
		Request::Serialize(MTPphone_InviteToGroupCall(call, MTP_vector<MTPInputUser>({ MTP_inputUser(MTP_long(42), MTP_long(1)) }))),
		Request::Serialize(MTPphone_CreateConferenceCall(MTP_flags(0), MTP_int(1), MTPint256(), MTPbytes(), MTPDataJSON())),
	}) {
		checkGroup("group ownership cannot authorize admin/create/invite/conference", request, false, context);
	}
	eligible = false;
	checkGroup("revoked group join denied", joined, false, context);
	for (const auto &request : activeRequests) {
		checkGroup("revoked group media/read denied", request, false, context);
	}
	checkGroup("revoked own call cleanup admitted", Request::Serialize(
		MTPphone_LeaveGroupCall(call, MTP_int(100))), true, context);
	checkGroup("revoked unbound cleanup denied", Request::Serialize(
		MTPphone_LeaveGroupCall(other, MTP_int(100))), false, context);
	eligible = true;
	checkGroup("reallow does not resurrect old ownership", joined, false, context);
	checkProof(context.begin(1, 1), "fresh explicit group join permitted after reallow");
	checkGroup("fresh rejoin admitted", joined, true, context);
	context.close(1, 1);
	checkGroup("closed group cannot transmit", joined, false, context);
	context.forget(1, 1);
	checkGroup("forgotten group cannot transmit", joined, false, context);
	checkGroup("forgotten group cannot leave", Request::Serialize(
		MTPphone_LeaveGroupCall(call, MTP_int(100))), false, context);
	checkGroup("ordinary file download retained", Request::Serialize(MTPupload_GetFile(
		MTP_flags(0), MTP_inputDocumentFileLocation(MTP_long(1), MTP_long(1), MTP_bytes(""), MTP_string("")),
		MTP_long(0), MTP_int(131072))), true, context);
}
