// Included only in the disposable AllowlistLockWidget test overlay.
// Uses the initialized production widget, styles, fonts and event handlers.
const auto reportPath = qEnvironmentVariable("ALLOWGRAM_UI_REPORT");
if (reportPath.isEmpty()) {
	return;
}
auto checks = QJsonArray();
auto measurements = QJsonArray();
auto failures = 0;
const auto check = [&](bool pass, const QString &name) {
	checks.append(QJsonObject{ { "name", name }, { "pass", pass } });
	failures += pass ? 0 : 1;
};
const auto settle = [] {
	QCoreApplication::sendPostedEvents();
	QCoreApplication::processEvents();
};
const auto removeButton = [](IdRow *row) {
	for (const auto child : row->children()) {
		if (const auto button = dynamic_cast<Ui::LinkButton*>(child)) {
			return button;
		}
	}
	return static_cast<Ui::LinkButton*>(nullptr);
};
const auto inspect = [&](IdRow *row, const QString &state) {
	const auto field = row->field();
	const auto edit = field->rawTextEdit();
	field->finishAnimating();
	settle();
	const auto viewport = edit->viewport()->rect();
	const auto documentHeight = edit->document()->size().height();
	const auto caret = edit->cursorRect();
	const auto button = removeButton(row);
	const auto editable = field->rect().marginsRemoved(field->fullTextMargins());
	check(viewport.height() >= std::ceil(documentHeight), state + ": full text line fits");
	check(caret.top() >= viewport.top() && caret.bottom() <= viewport.bottom(),
		state + ": caret vertically contained");
	check(button && std::abs(button->geometry().center().y() - editable.center().y()) <= 1,
		state + ": Remove centered on editable region");
	check(button && !button->geometry().intersects(field->geometry()),
		state + ": Remove does not overlap field");
	check(button && row->rect().contains(button->geometry()), state + ": Remove contained");
	check(row->rect().contains(field->geometry()), state + ": field contained");
	check(editable.bottom() < field->height() - field->st().borderActive,
		state + ": active underline clear of text");
	measurements.append(QJsonObject{
		{ "state", state },
		{ "fieldHeight", field->height() },
		{ "viewportHeight", viewport.height() },
		{ "documentHeight", documentHeight },
		{ "caretHeight", caret.height() },
		{ "editableCenter", editable.center().y() },
		{ "removeCenter", button ? button->geometry().center().y() : -1 },
	});
};
const auto scale = style::Scale();
check(scale == qEnvironmentVariableIntValue("ALLOWGRAM_UI_SCALE"), "requested style scale applied");
check(!this->window()->maybeSession(), "isolated unsigned-in widget");
resize(qEnvironmentVariableIntValue("ALLOWGRAM_UI_WIDTH"),
	qEnvironmentVariableIntValue("ALLOWGRAM_UI_HEIGHT"));
show();
settle();
auto reference = Ui::InputField(this, st::defaultInputField,
	Ui::InputField::Mode::SingleLine, rpl::single(u"Reference"_q), u"123456789"_q);
reference.resizeToWidth(_users.front()->field()->width());
reference.show();
settle();
check(reference.rawTextEdit()->viewport()->height()
	>= std::ceil(reference.rawTextEdit()->document()->size().height()),
	"upstream default field fits the same font");
reference.hide();
inspect(_users.front(), u"empty"_q);
_users.front()->field()->setFocus();
inspect(_users.front(), u"focused"_q);
for (const auto &value : { u"8558994389"_q, u"1099511627775"_q }) {
	_users.front()->field()->setTextWithTags({ value, {} });
	inspect(_users.front(), value);
	_users.front()->field()->selectAll();
	check(_users.front()->field()->rawTextEdit()->textCursor().selectedText() == value,
		u"selection retains "_q + value);
}
for (const auto &value : { u"-123456789"_q, u"-1001234567890"_q,
	u"chat:123456789"_q, u"channel:1099511627775"_q }) {
	_groups.front()->field()->setTextWithTags({ value, {} });
	inspect(_groups.front(), value);
}
_users.front()->field()->setTextWithTags({ u"@example_bot"_q, {} });
_users.front()->field()->showError();
inspect(_users.front(), u"invalid"_q);
check(removeButton(_users.front())->isDisabled(), "last user Remove disabled");
check(removeButton(_groups.front())->isDisabled(), "last group Remove disabled");
removeRow(true, _users.front());
removeRow(false, _groups.front());
check(_users.size() == 1 && _groups.size() == 1, "last rows protected");
_addUser->clicked(Qt::NoModifier, Qt::LeftButton);
_addGroup->clicked(Qt::NoModifier, Qt::LeftButton);
settle();
check(_users.size() == 2 && _groups.size() == 2, "both plus buttons add rows");
check(!removeButton(_users.front())->isDisabled(), "multiple rows enable Remove");
auto enter = QKeyEvent(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
QCoreApplication::sendEvent(_users.back()->field()->rawTextEdit(), &enter);
settle();
check(_users.size() == 3, "Enter adds user row");
_users.front()->field()->setFocus();
auto tab = QKeyEvent(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
QCoreApplication::sendEvent(_users.front()->field()->rawTextEdit(), &tab);
settle();
check(!_users.front()->field()->rawTextEdit()->hasFocus(), "Tab advances focus");
removeButton(_users.back())->clicked(Qt::NoModifier, Qt::LeftButton);
settle();
check(_users.size() == 2, "Remove callback removes user row");
removeButton(_groups.back())->clicked(Qt::NoModifier, Qt::LeftButton);
settle();
check(_groups.size() == 1, "Remove callback removes group row");
for (auto i = 0; i != 12; ++i) {
	_addGroup->clicked(Qt::NoModifier, Qt::LeftButton);
}
settle();
const auto last = _groups.back();
const auto lastTop = last->mapTo(_layout, QPoint()).y();
check(_scroll->scrollTop() <= lastTop
	&& _scroll->scrollTop() + _scroll->height() >= lastTop + last->height(),
	"new row scrolled into view");
const auto fieldWidth = _groups.front()->field()->width();
auto previousBottom = -1;
for (const auto row : _groups) {
	check(row->field()->width() == fieldWidth, "repeated field widths equal");
	check(row->y() > previousBottom, "repeated rows do not overlap");
	previousBottom = row->geometry().bottom();
}
showError(QString(6, QChar(' ')).replace(u" "_q,
	u"Invalid ID: @example_bot. Use numeric Telegram IDs; usernames are not accepted. "_q));
settle();
check(_error->height() > st::normalFont->height, "long validation wraps");
const auto errorTop = _error->mapTo(_layout, QPoint()).y();
check(_scroll->scrollTop() <= errorTop
	&& _scroll->scrollTop() + _scroll->height() >= errorTop + _error->height(),
	"long validation accessible by scroll");
_scroll->scrollToY(_scroll->scrollTopMax());
settle();
const auto saveTop = submit->mapTo(_layout, QPoint()).y();
check(_scroll->scrollTop() <= saveTop
	&& _scroll->scrollTop() + _scroll->height() >= saveTop + submit->height(),
	"Save accessible at bottom");
auto report = QFile(reportPath);
if (report.open(QIODevice::WriteOnly)) {
	report.write(QJsonDocument(QJsonObject{
		{ "scale", scale }, { "devicePixelRatio", devicePixelRatioF() },
		{ "width", width() }, { "height", height() },
		{ "checks", checks }, { "measurements", measurements },
		{ "failures", failures },
	}).toJson());
	report.close();
}
QCoreApplication::exit(failures ? 1 : 0);
