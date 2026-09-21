#pragma once

#include <QtCore/QCoreApplication>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QTimer>
#include <QtCore/QVariant>
#include <QtGui/QKeyEvent>

namespace SheetWidgetTest {

inline void Check(bool passed, const char *name) {
	const auto app = QCoreApplication::instance();
	auto checks = app->property("sheetChecks").toJsonArray();
	checks.append(QJsonObject{ { "name", name }, { "pass", passed } });
	app->setProperty("sheetChecks", checks);
	if (!passed) {
		app->setProperty("sheetFailures", app->property("sheetFailures").toInt() + 1);
	}
}

inline void Finish() {
	const auto app = QCoreApplication::instance();
	if (app->property("sheetFinished").toBool()) {
		return;
	}
	app->setProperty("sheetFinished", true);
	auto output = QFile(qEnvironmentVariable("ALLOWGRAM_SHEET_REPORT"));
	const auto failures = app->property("sheetFailures").toInt();
	const auto bytes = QJsonDocument(QJsonObject{
		{ "checks", app->property("sheetChecks").toJsonArray() },
		{ "failures", failures },
		{ "finished", true },
	}).toJson();
	if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size()) {
		app->exit(2);
		return;
	}
	output.close();
	QTimer::singleShot(0, app, [=] { app->exit(failures ? 1 : 0); });
}

} // namespace SheetWidgetTest
