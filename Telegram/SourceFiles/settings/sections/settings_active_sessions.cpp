/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/sections/settings_active_sessions.h"

#include "settings/sections/settings_main.h"

#include "ui/painter.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_settings_active_sessions.h"

namespace Settings {
void AddSessionInfoRow(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> label,
		const QString &value,
		const style::icon &icon) {
	if (value.isEmpty()) {
		return;
	}

	const auto text = container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			rpl::single(value),
			st::boxLabel),
		st::boxRowPadding + st::sessionValuePadding);
	const auto left = st::sessionValuePadding.left();
	container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			std::move(label),
			st::sessionValueLabel),
		(st::boxRowPadding
			+ style::margins{ left, 0, 0, st::sessionValueSkip }));

	const auto widget = Ui::CreateChild<Ui::RpWidget>(container.get());
	widget->resize(icon.size());

	text->topValue() | rpl::on_next([=](int top) {
		widget->move(st::sessionValueIconPosition + QPoint(0, top));
	}, widget->lifetime());

	widget->paintRequest() | rpl::on_next([=, &icon] {
		auto p = QPainter(widget);
		icon.paintInCenter(p, widget->rect());
	}, widget->lifetime());
}

Type SessionsId() {
	// Cached settings links must not restore the removed Devices section.
	// Main settings still exposes the current account's own Log Out action.
	return MainId();
}

} // namespace Settings
