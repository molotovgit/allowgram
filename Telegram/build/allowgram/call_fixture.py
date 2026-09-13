"""Disposable private-call effects and synthetic server replies for native tests."""
from pathlib import Path


def replace_once(text, old, new):
    assert text.count(old) == 1, old[:120]
    return text.replace(old, new)


def body(text, signature, replacement):
    start = text.index('{', text.index(signature)) + 1
    end = text.index('\n}', start)
    return text[:start] + '\n' + replacement + text[end:]


def probe(name):
    return ('QCoreApplication::instance()->setProperty("' + name
            + '", QCoreApplication::instance()->property("' + name + '").toInt() + 1);')


def instrument_calls(root, includes):
    instance = (root / 'Telegram/SourceFiles/calls/calls_instance.cpp').read_text(encoding='utf-8')
    instance = replace_once(instance, '\tusing Type = Platform::PermissionType;',
        '\t' + probe('allowgramCallPermissions') + '\n\tonSuccess();\n\treturn;\n\tusing Type = Platform::PermissionType;')
    for anchor, name in (
        ('\tconfirmLeaveCurrent(show, peer, args, [=](StartGroupCallArgs args) {', 'allowgramGroupStart'),
        ('\tExpects(args.call || args.show);', 'allowgramConferenceStart'),
        ('\t_startWithRtmp->start(peer, show, [=](Group::JoinInfo info) {', 'allowgramRtmpStart')):
        instance = replace_once(instance, anchor, '\t' + probe(name) + '\n\treturn;\n' + anchor)
    instance = body(instance, 'void Instance::destroyCall(', '''
    if (_currentCall.get() == call) {
        auto taken = base::take(_currentCall);
        taken.reset();
    }''')
    instance = instance.replace('_currentCallPanel->replaceCall(raw);', '')
    instance = instance.replace('_currentCallPanel = std::make_unique<Panel>(raw);', probe('allowgramCallPresentation'))
    instance = instance.replace('_currentCallPanel->closeBeforeDestroy();', probe('allowgramCallPresentationClosed'))
    instance = instance.replace('_currentCallPanel->showAndActivate();', probe('allowgramCallPresentation'))
    instance = instance.replace('_currentCallChanges.fire_copy(raw);', '')
    start = instance.index('\t\t\t_currentCallPanel->startOutgoingRequests(')
    end = instance.index('\n\t\t} else {', start)
    instance = instance[:start] + '\t\t\t' + probe('allowgramCallConfirmation') + instance[end:]
    anchor = '\tExpects(_currentCall != nullptr);'
    instance = replace_once(instance, anchor, '''
    _cachedDhConfig->g = 3;
    _cachedDhConfig->p = bytes::vector(256, gsl::byte{1});
    _currentCall->start(bytes::vector(256, gsl::byte{1}));
    return;
''' + anchor)
    instance = body(instance, 'bool Instance::hasVisiblePanel(', '\treturn false;')
    instance = body(instance, 'bool Instance::hasActivePanel(', '\treturn false;')
    instance = body(instance, 'void Instance::Delegate::callPlaySound(', '\t' + probe('allowgramCallSound'))
    call = (root / 'Telegram/SourceFiles/calls/calls_call.cpp').read_text(encoding='utf-8')
    call = body(call, 'void Call::generateModExpFirst(', '''
    _ga = bytes::vector(256, gsl::byte{1});
    _gb = bytes::vector(256, gsl::byte{1});
    _gaHash = bytes::vector(32, gsl::byte{1});''')
    call = body(call, 'void Call::setupMediaDevices(', '\t' + probe('allowgramCallDeviceSetup'))
    call = body(call, 'void Call::setupOutgoingVideo(', '\t' + probe('allowgramCallVideoSetup'))
    call = body(call, 'void Call::destroyController(', '\t' + probe('allowgramCallControllerStop') + '\n\t_instanceLifetime.destroy();\n\t_instance.reset();')
    anchor = '\t_waitingTrack = Media::Audio::Current().createTrack();'
    call = replace_once(call, anchor, '\t' + probe('allowgramCallRing') + '\n\treturn;\n' + anchor)
    for signature in ('void Call::confirmAcceptedCall(', 'void Call::startConfirmedCall('):
        start = call.index('\tconst auto firstBytes = ', call.index(signature))
        end = call.index('\n\t_keyFingerprint = ComputeFingerprint(_authKey);', start) + len('\n\t_keyFingerprint = ComputeFingerprint(_authKey);')
        call = call[:start] + '\t_keyFingerprint = 1;' + call[end:]
    anchor = '\t_conferenceSupported = false;'
    call = replace_once(call, anchor, anchor + '\n\t' + probe('allowgramCallControllerStart')
        + '\n\tsetState(State::Established);\n\tsendSignalingData(QByteArray("synthetic"));\n\treturn;')
    call = replace_once(call, '|| data.vphone_call_id().v != _id || !_instance)',
        '|| data.vphone_call_id().v != _id || state() != State::Established)')
    call = replace_once(call, '\t_instance->receiveSignalingData(std::move(prepared));',
        '\t' + probe('allowgramCallSignalingReceived'))
    error_anchor = '\tconst auto inform = (error == u"USER_PRIVACY_RESTRICTED"_q)'
    call = replace_once(call, error_anchor, '\t' + probe('allowgramCallErrorPresentation') + '\n' + error_anchor)
    return [('calls_instance', 'calls/calls_instance.cpp', includes + '\n' + instance),
            ('calls_call', 'calls/calls_call.cpp', includes + '\n' + call)]


def instrument_transport(root, includes):
    source = (root / 'Telegram/SourceFiles/mtproto/mtp_instance.cpp').read_text(encoding='utf-8')
    anchor = 'if (_requestFilter && !_requestFilter(request)) {'
    code = r'''
    const auto allowed = !_requestFilter || _requestFilter(request);
    const auto app = QCoreApplication::instance();
    const auto offset = details::SerializedRequest::kMessageBodyPosition;
    const auto type = request->size() > offset ? mtpTypeId((*request)[offset]) : mtpTypeId(0);
    auto callRequest = false;
    switch (type) {
    case mtpc_phone_requestCall:
    case mtpc_phone_receivedCall:
    case mtpc_phone_acceptCall:
    case mtpc_phone_confirmCall:
    case mtpc_phone_discardCall:
    case mtpc_phone_sendSignalingData:
    case mtpc_phone_getCallConfig:
    case mtpc_messages_getDhConfig:
        callRequest = true;
        break;
    }
    if (callRequest) {
        const auto property = QByteArray("allowgramRpc_") + QByteArray::number(type) + (allowed ? "_allowed" : "_denied");
        app->setProperty(property.constData(), app->property(property.constData()).toInt() + 1);
    }
    if (callRequest && allowed && app->property("allowgramSyntheticCallReplies").toBool()) {
        request->requestId = requestId;
        storeRequest(requestId, request, std::move(callbacks));
        if (type == mtpc_phone_sendSignalingData && app->property("allowgramDeferSignaling").toBool()) {
            app->setProperty("allowgramPendingSignal", requestId);
            return;
        }
        auto response = Response{ .requestId = requestId };
        auto from = request->constData() + offset + 1;
        const auto end = request->constData() + request->size();
        auto input = MTPInputPhoneCall();
        const auto protocol = MTP_phoneCallProtocol(MTP_flags(0), MTP_int(65), MTP_int(100), MTP_vector<MTPstring>({ MTP_string("synthetic") }));
        if (type == mtpc_phone_requestCall) {
            const auto id = app->property("allowgramNextCallId").toULongLong() + 1;
            app->setProperty("allowgramNextCallId", QVariant::fromValue<qulonglong>(id));
            app->setProperty("allowgramActiveCallId", QVariant::fromValue<qulonglong>(id));
            MTPphone_PhoneCall(MTP_phone_phoneCall(
                MTP_phoneCallWaiting(MTP_flags(0), MTP_long(id), MTP_long(id + 100), MTP_int(1700000000),
                    MTP_long(99), MTP_long(42), protocol, MTPint()), MTP_vector<MTPUser>({}))).write(response.reply);
        } else if (type == mtpc_phone_acceptCall || type == mtpc_phone_confirmCall) {
            if (!input.read(from, end)) Unexpected("Invalid synthetic phone input");
            const auto &data = input.data();
            const auto call = type == mtpc_phone_acceptCall
                ? MTPPhoneCall(MTP_phoneCallWaiting(MTP_flags(0), data.vid(), data.vaccess_hash(), MTP_int(1700000000),
                    MTP_long(42), MTP_long(99), protocol, MTPint()))
                : MTPPhoneCall(MTP_phoneCall(MTP_flags(0), data.vid(), data.vaccess_hash(), MTP_int(1700000000),
                    MTP_long(99), MTP_long(42), MTP_bytes(QByteArray(256, 'a')), MTP_long(1), protocol,
                    MTP_vector<MTPPhoneConnection>({}), MTP_int(1700000000), MTPDataJSON()));
            MTPphone_PhoneCall(MTP_phone_phoneCall(call, MTP_vector<MTPUser>({}))).write(response.reply);
        } else if (type == mtpc_phone_discardCall) {
            MTPUpdates(MTP_updatesTooLong()).write(response.reply);
        } else if (type == mtpc_phone_getCallConfig) {
            MTPDataJSON(MTP_dataJSON(MTP_string("{}"))).write(response.reply);
        } else {
            MTPBool(MTP_boolTrue()).write(response.reply);
        }
        crl::on_main(_instance, [=, response = std::move(response)] { processCallback(response); });
        return;
    }
    if (true) {
'''
    source = replace_once(source, anchor, code)
    source = replace_once(source, 'void Instance::Private::cancel(mtpRequestId requestId) {', '''void Instance::Private::cancel(mtpRequestId requestId) {
    const auto pendingCall = getRequest(requestId);
    if (pendingCall && pendingCall->size() > details::SerializedRequest::kMessageBodyPosition
        && (*pendingCall)[details::SerializedRequest::kMessageBodyPosition] == mtpc_phone_sendSignalingData) {
        QCoreApplication::instance()->setProperty("allowgramSignalCancelled",
            QCoreApplication::instance()->property("allowgramSignalCancelled").toInt() + 1);
    }
''')
    return ('mtp_instance', 'mtproto/mtp_instance.cpp', includes + '\n' + source)