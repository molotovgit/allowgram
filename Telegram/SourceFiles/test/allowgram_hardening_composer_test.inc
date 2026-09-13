auto checks = QJsonArray();
auto failures = 0;
const auto check = [&](bool pass, const char *name) {
	checks.push_back(QJsonObject{ { "name", name }, { "pass", pass } });
	failures += pass ? 0 : 1;
	auto progress = QFile(qEnvironmentVariable("ALLOWGRAM_HARDENING_REPORT") + u".composer.json"_q);
	if (progress.open(QIODevice::WriteOnly)) {
		progress.write(QJsonDocument(QJsonObject{ { "checks", checks }, { "failures", failures }, { "finished", false } }).toJson());
	}
};
auto submissions = 0;
check(_historyInited && _list && _forwardPanel && _keyboard, "real composer is initialized before send callbacks");
if (!_historyInited || !_list || !_forwardPanel || !_keyboard) {
	return;
}
auto observation = rpl::lifetime();
session().api().sendActions() | rpl::on_next([&] { ++submissions; }, observation);
for (const auto &text : { u"pasted \U0001F600"_q, u"family \U0001F468\u200D\U0001F469\u200D\U0001F467"_q, u"keycap 1\uFE0F\u20E3"_q }) {
	_field->setTextWithTags({ text, {} });
	const auto done = std::make_shared<bool>(false);
	sendTextWithTags(_field->getTextWithTags(), false, {}, [done] { *done = true; });
	check(_field->getTextWithTags().text == text, "production blocked-send callback retains exact draft");
	check(!*done, "blocked-send callback does not report success");
}
const auto plain = u"ordinary body with selected quote"_q;
_field->setTextWithTags({ plain, {} });
const auto quoted = session().data().message(_history->peer, MsgId(17));
check(quoted != nullptr, "production reply source exists");
replyToMessage(quoted, { .quote = { u"selected \U0001F600"_q, {} } });
check(_replyTo.quote.text == u"selected \U0001F600"_q, "real reply UI accepts the selected source quote");
const auto quoteBefore = _replyTo;
sendTextWithTags(_field->getTextWithTags(), false, {}, nullptr);
check(_field->getTextWithTags().text == plain, "emoji quote rejection retains body");
check(_replyTo == quoteBefore, "emoji quote rejection retains exact selected quote");
check(submissions == 0, "blocked body and quote produce zero API send submissions");
cancelReply();
_field->setTextWithTags({ plain, {} });
const auto page = std::make_shared<Iv::RichPage>(Iv::SplitTextIntoRichPage(TextWithTags{ plain, {} }));
const auto beforeRich = submissions;
sendRichDraft(page, {});
check(_field->getTextWithTags().text == plain, "opaque rich rejection retains compose text");
check(submissions == beforeRich, "opaque rich rejection produces zero API send submissions");
replyToMessage(quoted, {
	.quote = { u"ordinary selected quote"_q, {} },
	.quoteOffset = int(quoted->originalText().text.indexOf(u"ordinary selected quote"_q)),
});
const auto beforePlain = submissions;
sendTextWithTags(_field->getTextWithTags(), false, {}, nullptr);
check(submissions > beforePlain, "ordinary quote reaches the real send pipeline with networking disabled");
check(_field->getTextWithTags().text.isEmpty(), "accepted ordinary quote clears the sent compose text");
check(_tabbedSelectorToggle->isHidden(), "production composer picker is hidden");
toggleTabbedSelectorMode();
check(_tabbedSelectorToggle->isHidden(), "keyboard-equivalent picker callback does not restore toggle");
check(_field->width() > 0, "composer retains positive input width");
check(QCoreApplication::instance()->property("allowgramCallButtonSeen").toBool(), "native top-bar visibility observation ran");
check(QCoreApplication::instance()->property("allowgramCallButtonVisible").toBool(), "allowed DM top bar exposes its private call action");
auto candidate = Ui::PreparedFile(QString());
candidate.content = "%PDF-1.7 ordinary late upload";
check(AllowgramSendFileAllowed(candidate), "byte-only late upload candidate does not assert");
candidate.content = "GIF89a renamed late upload";
check(!AllowgramSendFileAllowed(candidate), "byte-only late GIF upload remains denied");
candidate.content = QByteArray();
check(!AllowgramSendFileAllowed(candidate), "opaque empty upload candidate denied");
auto report = QFile(qEnvironmentVariable("ALLOWGRAM_HARDENING_REPORT") + u".composer.json"_q);
if (report.open(QIODevice::WriteOnly)) {
	report.write(QJsonDocument(QJsonObject{ { "checks", checks }, { "failures", failures }, { "finished", true } }).toJson());
}
