#include "StartupHandler.h"
#include "Config.h"
#include "LogHelper.h"
#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QMimeData>
#include <QRegularExpression>
#include <QUrl>

StartupOptions StartupHandler::parse(const QStringList &args) {
	StartupOptions options;
	options.valid = true;

	QString arg1;
	if (args.size() > 1) {
		arg1 = args[1];
	} else {
		options.valid = false;
		options.errorMessage = "No arguments provided.";
		return options;
	}

	if (arg1 == "--settings") {
		options.showSettings = true;
		return options;
	}

	if (arg1 == "help" || arg1 == "--help") {
		options.showHelp = true;
		return options;
	}

	// Determine Mode and Dest from Args
	QString destDir;
	if (arg1 == "cp") {
		options.mode = OperationMode::Copy;
		if (args.size() > 2)
			destDir = args[2];
	} else if (arg1 == "mv") {
		options.mode = OperationMode::Move;
		if (args.size() > 2)
			destDir = args[2];
	} else if (args.size() > 2 && arg1 == "--paste-to") {
		destDir = args[2];
		// Mode determined by clipboard later
	}

	// Clipboard Logic
	// Get Clipboard Data
	// URI Lists vs. Plain Text:
	// Standard Copy: When you Ctrl+C a file, Dolphin populates the clipboard with text/uri-list.
	// hasUrls() check looks specifically for this format.
	// Copy Location: This action usually populates the clipboard as text/plain (raw string text).
	// Since a raw string is not technically a "URL list," mimeData->hasUrls() returns false.
	if (!Config::DRY_RUN) {
		const QClipboard *clipboard = QApplication::clipboard();
		const QMimeData *mimeData = clipboard->mimeData();

		// 	LOG(LogLevel::DEBUG) << "Available Formats:" << mimeData->formats().join(", ").toStdString();
		// 	LOG(LogLevel::DEBUG) << "Clipboard Text:" << clipboard->text();

		if (mimeData->hasUrls()) {
			QList<QUrl> urlList = mimeData->urls();
			for (const QUrl &url : urlList) {
				if (url.isLocalFile()) {
					options.sources.push_back(url.toLocalFile().toStdString());
				}
			}
		} else if (mimeData->hasText()) {
			QString rawText = mimeData->text();
			QStringList lines = rawText.split(QRegularExpression("[\r\n]+"), Qt::SkipEmptyParts);
			for (const QString &line : lines) {
				QString trimmed = line.trimmed();
				if (trimmed.startsWith("file://")) {
					trimmed = QUrl(trimmed).toLocalFile();
				}
				if (QFile::exists(trimmed)) {
					options.sources.push_back(trimmed.toStdString());
				}
			}
		}

		if (options.sources.empty()) {
			options.valid = false;
			options.errorMessage = tr("No files found in clipboard!");
			return options;
		}
	}

	if (destDir.isEmpty()) {
		options.valid = false;
		options.errorMessage = tr("No destination directory provided!");
		return options;
	}

	QDir dest(destDir);
	if (!dest.exists()) {
		options.valid = false;
		options.errorMessage = tr("Destination directory does not exist!");
		return options;
	}
	options.dest = destDir.toStdString();

	// Detect Mode from Clipboard if not explicitly set via cp/mv
	if (arg1 != "cp" && arg1 != "mv") {
		ClipboardAction action = detectClipboardAction();
		options.mode = (action == ClipboardAction::Move) ? OperationMode::Move : OperationMode::Copy;
	}

	return options;
}

StartupHandler::ClipboardAction StartupHandler::detectClipboardAction() {
	const QClipboard *clipboard = QApplication::clipboard();
	const QMimeData *mimeData = clipboard->mimeData();

	if (!mimeData)
		return ClipboardAction::Copy;

	QStringList cutFormats = {
		"application/x-kde-cutselection",
		"application/x-kde-cut-selection",
		"x-kde-cut-selection",
		"x-kde-cutselection"};

	for (const QString &format : cutFormats) {
		if (mimeData->hasFormat(format)) {
			QByteArray data = mimeData->data(format);
			if (data.contains('1')) {
				return ClipboardAction::Move;
			}
		}
	}

	// GNOME (Nautilus) uses a different convention: a "x-special/gnome-copied-files" format
	// where the first line of the data is "cut" or "copy"
	if (mimeData->hasFormat("x-special/gnome-copied-files")) {
		QByteArray data = mimeData->data("x-special/gnome-copied-files");
		if (data.startsWith("cut")) {
			return ClipboardAction::Move;
		}
	}

	return ClipboardAction::Copy;
}
