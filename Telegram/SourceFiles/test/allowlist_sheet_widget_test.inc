const auto scene = qEnvironmentVariable("ALLOWGRAM_SHEET_SCENE");
SheetWidgetTest::Check(_users.empty() && _groups.empty() && !_manual,
	"resolving creates no manual input fields");
setInnerFocus();
submit();
for (const auto key : { Qt::Key_Escape, Qt::Key_Return }) {
	auto event = QKeyEvent(QEvent::KeyPress, key, Qt::NoModifier);
	QCoreApplication::sendEvent(this, &event);
}
SheetWidgetTest::Check(!this->window()->maybeSession()->allowlistConfigured(),
	"focus, direct submit, Escape and Enter retain the initial lock");
QCoreApplication::instance()->setProperty("sheetSettingsBefore",
	this->window()->maybeSession()->settings().serialize());
if (scene == u"phone-change"_q) {
	QTimer::singleShot(30, this, [=] {
		this->window()->maybeSession()->user()->setPhone(u"998000000002"_q);
	});
}
const auto timer = new QTimer(this);
timer->setInterval(20);
connect(timer, &QTimer::timeout, this, [=] {
	const auto session = this->window()->maybeSession();
	if (_manual) {
		timer->stop();
		SheetWidgetTest::Check(scene == u"absent"_q,
			"only authoritative not_found creates manual setup");
		SheetWidgetTest::Check(_users.size() == 1 && _groups.size() == 1,
			"existing manual setup starts with its original fields");
		_users.front()->field()->setTextWithTags({ u"0"_q, {} });
		submit();
		SheetWidgetTest::Check(!session->allowlistConfigured() && !_error->isHidden(),
			"manual invalid ID cannot unlock");
		_users.front()->field()->setTextWithTags({ u"201"_q, {} });
		addRow(true, false);
		_users.back()->field()->setTextWithTags({ u"202"_q, {} });
		addRow(true, false);
		_users.back()->field()->setTextWithTags({ u"203"_q, {} });
		addRow(true, false);
		removeRow(true, _users.back());
		SheetWidgetTest::Check(_users.size() == 3, "manual add and remove rows retained");
		_groups.front()->field()->setTextWithTags({ u"-1000000000101"_q, {} });
		addRow(false, false);
		_groups.back()->field()->setTextWithTags({ u"-1000000000102"_q, {} });
		submit();
		return;
	}
	if (_retry && !_retry->isDisabled()) {
		timer->stop();
		SheetWidgetTest::Check(scene != u"matched"_q && scene != u"absent"_q,
			"failure is expected in this scenario");
		SheetWidgetTest::Check(_users.empty() && _groups.empty(),
			"configuration, HTTP and save errors never expose manual fields");
		SheetWidgetTest::Check(!session->allowlistConfigured() && session->allowlistPeers().empty(),
			"failure never applies policy peers or unlocks");
		SheetWidgetTest::Check(session->settings().serialize()
			== QCoreApplication::instance()->property("sheetSettingsBefore").toByteArray(),
			"save failure leaves existing settings unchanged");
		submit();
		SheetWidgetTest::Check(!session->allowlistConfigured(), "direct submit cannot bypass failed lookup");
		SheetWidgetTest::Finish();
	}
});
timer->start();
