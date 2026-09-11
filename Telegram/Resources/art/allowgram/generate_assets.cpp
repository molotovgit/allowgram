#include <QtCore/QBuffer>
#include <QtCore/QCoreApplication>
#include <QtCore/QDataStream>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QXmlStreamReader>
#include <QtGui/QImage>
#include <QtGui/QPainter>
#include <QtSvg/QSvgRenderer>

#include <array>
#include <iostream>

namespace {

QImage Render(QSvgRenderer &renderer, int size) {
	auto image = QImage(size, size, QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	auto painter = QPainter(&image);
	painter.setRenderHint(QPainter::Antialiasing);
	renderer.render(&painter, QRectF(0, 0, size, size));
	return image;
}

bool Write(const QString &path, const QByteArray &data) {
	auto file = QFile(path);
	return file.open(QIODevice::WriteOnly)
		&& file.write(data) == data.size();
}

bool Generate(const QDir &directory) {
	auto source = QFile(directory.filePath("mark.svg"));
	if (!source.open(QIODevice::ReadOnly)) {
		return false;
	}
	const auto svg = source.readAll();
	auto renderer = QSvgRenderer(svg);
	if (!renderer.isValid()) {
		return false;
	}
	const auto sizes = std::array{ 16, 20, 24, 32, 40, 48, 64, 128, 256 };
	auto frames = std::array<QByteArray, sizes.size()>();
	for (auto i = std::size_t(0); i != sizes.size(); ++i) {
		auto buffer = QBuffer(&frames[i]);
		if (!buffer.open(QIODevice::WriteOnly)
			|| !Render(renderer, sizes[i]).save(&buffer, "PNG")) {
			return false;
		}
	}
	auto ico = QByteArray();
	auto stream = QDataStream(&ico, QIODevice::WriteOnly);
	stream.setByteOrder(QDataStream::LittleEndian);
	stream << quint16(0) << quint16(1) << quint16(sizes.size());
	auto offset = quint32(6 + 16 * sizes.size());
	for (auto i = std::size_t(0); i != sizes.size(); ++i) {
		const auto dimension = quint8(sizes[i] == 256 ? 0 : sizes[i]);
		stream << dimension << dimension << quint8(0) << quint8(0)
			<< quint16(1) << quint16(32)
			<< quint32(frames[i].size()) << offset;
		offset += quint32(frames[i].size());
	}
	for (const auto &frame : frames) {
		stream.writeRawData(frame.constData(), frame.size());
	}
	if (stream.status() != QDataStream::Ok
		|| !Write(directory.filePath("allowgram.ico"), ico)
		|| !Write(directory.filePath("logo_256.png"), frames.back())) {
		return false;
	}
	renderer.setViewBox(QRectF(16, 16, 224, 224));
	if (!Render(renderer, 256).save(
		directory.filePath("logo_256_no_margin.png"),
		"PNG")) {
		return false;
	}
	auto xml = QXmlStreamReader(svg);
	auto bubble = QString();
	auto check = QString();
	while (!xml.atEnd()) {
		xml.readNext();
		if (xml.isStartElement() && xml.name() == QStringLiteral("path")) {
			const auto attributes = xml.attributes();
			const auto id = attributes.value("id");
			if (id == QStringLiteral("bubble")) {
				bubble = attributes.value("d").toString();
			} else if (id == QStringLiteral("check")) {
				check = attributes.value("d").toString();
			}
		}
	}
	if (xml.hasError() || bubble.isEmpty() || check.isEmpty()) {
		return false;
	}
	const auto start = QStringLiteral(
		"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"16\" height=\"16\" "
		"viewBox=\"0 0 256 256\">\n"
		"  <path fill=\"#FFFFFF\" fill-rule=\"evenodd\" d=\"%1 %2\"/>\n"
	).arg(bubble, check);
	const auto suffixes = std::array{ "", "_attention", "_mute" };
	const auto colors = std::array{ "", "#f23c34", "#888888" };
	for (auto i = std::size_t(0); i != suffixes.size(); ++i) {
		auto content = start;
		if (i) {
			content += QStringLiteral(
				"  <circle fill=\"%1\" cx=\"62.4\" cy=\"203.2\" r=\"35.2\"/>\n"
			).arg(colors[i]);
		}
		content += QStringLiteral("</svg>\n");
		const auto name = QStringLiteral("tray_monochrome%1.svg").arg(suffixes[i]);
		if (!Write(
			directory.filePath(name),
			content.replace('\n', QStringLiteral("\r\n")).toUtf8())) {
			return false;
		}
	}
	return true;
}

} // namespace

int main(int argc, char *argv[]) {
	auto app = QCoreApplication(argc, argv);
	if (app.arguments().size() != 2
		|| !Generate(QDir(app.arguments().at(1)))) {
		std::cerr << "Usage: generate_assets <allowgram artwork directory>\n";
		return 1;
	}
	return 0;
}
