#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QLibraryInfo>
#include <QMessageBox>
#include <QMimeData>
#include <QTranslator>
#include <QUrl>
#include <QLockFile>

#include <QEventLoop>
#include <QTimer>
#include <iostream>

#include "Config.h"
#include "LogHelper.h"
#include "MainWindow.h"
#include "Settings.h"

using std::cout;
using std::endl;

// ------- Requirements to compile
// OpenSuse is my operating system
// sudo zypper install CMake gcc-c++ mold lld xxhash-devel \
// qt6-base-devel qt6-tools-devel qt6-widgets-devel
// sudo zypper install qt6-linguist-devel

// Ubuntu
// sudo apt install cmake g++ mold lld libxxhash-dev \
// qt6-base-dev qt6-base-dev-tools

// # From your project root:
// mkdir build && cd build
// cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=mold" ..
// make -j$(nproc)

// Workflow for translations
// Scan: Run the VSCodium task (CTRL+SHIFT+B).
// cmake --build ${command:cmake.buildDirectory} --target Movero_lupdate
// Translate: Open the .ts file in Qt Linguist and save.
// Build: The .qm is automatically compiled and put into :/i18n/ or /translations
// depending on the CMakeLists settings.

// Enum to match existing CopyWorker modes
enum class ClipboardAction {
	Copy,
	Move
};

ClipboardAction detectClipboardAction() {
	const QClipboard *clipboard = QApplication::clipboard();
	const QMimeData *mimeData = clipboard->mimeData();

	if (!mimeData) {
		LOG(LogLevel::DEBUG) << "Clipboard action fallback to Copy. No MIME data.";
		return ClipboardAction::Copy; // Default fallback
	}

	// List of known formats for 'Cut' in the KDE/Qt world
	QStringList cutFormats = {
		"application/x-kde-cutselection",
		"application/x-kde-cut-selection",
		"x-kde-cut-selection",
		"x-kde-cutselection"
	};

	for (const QString &format : cutFormats) {
		if (mimeData->hasFormat(format)) {
			QByteArray data = mimeData->data(format);

			// Log for debugging (remove in production)
			LOG(LogLevel::DEBUG) << "Found format:" << format << "Data:" << data;

			// '1' indicates a Cut operation
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


bool isXcbPluginAvailable() {
	// 1. Get the path where Qt expects platform plugins
	// On Linux, this is usually /usr/lib/qt/plugins/platforms
	QString pluginPath = QLibraryInfo::path(QLibraryInfo::PluginsPath) + "/platforms";

	// 2. The filename for the X11/XCB plugin
	QString xcbPlugin = pluginPath + "/libqxcb.so";

	// 3. Check if the file exists and is readable
	return QFile::exists(xcbPlugin);
}


int main(int argc, char *argv[]) {
	// Check if we are on a Wayland session
	bool isWayland = (qgetenv("XDG_SESSION_TYPE") == "wayland");

	if (isWayland) {
		if (isXcbPluginAvailable()) {
			// Force X11 compatibility mode to fix the offscreen position
			// of the details window when expanded. Wayland doesn't allow the app
			// from knowing it's position on the screen and doesn't manage window overflow
			// if resized more than available space.
			qputenv("QT_QPA_PLATFORM", "xcb");
			qDebug() << "Wayland detected. Forcing XCB/X11 mode.";
		} else {
			// XCB missing - app would crash if we forced it.
			// Let it run on Wayland natively.
			qDebug() << "Wayland detected but XCB plugin is missing. Using native Wayland.";
		}
	}

	QApplication app(argc, argv);

	LOG(LogLevel::INFO) << APP_NAME << "started.";
	LOG(LogLevel::INFO) << "Version" << APP_VERSION;

	// Create a temporary path for the lock file
	// A lock file is a temporary file created when the app starts. 
	// If a second instance tries to start, it sees the file exists and is 
	// "locked" by another Process ID (PID), so it exits.
	// If app crashes, QLockFile is smart enough to check if the PID stored in the lock file 
	// is still active. If the PID is dead, it will automatically break the old lock 
	// and let the new instance start.
    QString tmpDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QLockFile lockFile(tmpDir + "/" + APP_NAME + "_unique_lock.lock");

    // Try to lock the file. If it fails, another instance is running.
    if (!lockFile.tryLock(100)) { // Wait 100ms to be sure
        QMessageBox::warning(nullptr, 
			QCoreApplication::translate("Main","Already Running"), 
			QString::fromStdString(APP_NAME) + 
			QCoreApplication::translate("Main", " is already running. Please close the other instance first."));
        return 0; 
    }

	// Set global stylesheet
	QFile styleFile(":/style.qss");
	if (styleFile.open(QFile::ReadOnly)) {
		QString styleSheet = QLatin1String(styleFile.readAll());
		qApp->setStyleSheet(styleSheet);
	}

	app.setOrganizationName(APP_NAME);
	app.setApplicationName(APP_NAME);
	app.setDesktopFileName(QString(APP_NAME) + ".desktop");

	Config::load();
	QTranslator translator;
	QString tr_path = ":/translations/" + QString(APP_NAME) + "_" + Config::LANGUAGE + ".qm";
	if (translator.load(tr_path)) {
		if (QCoreApplication::installTranslator(&translator)) {
			LOG(LogLevel::INFO) << "Successfully installed " + Config::LANGUAGE + " translation.";
		} else {
			LOG(LogLevel::INFO) << "Failed to install translator.";
		}
	} else {
		if (Config::LANGUAGE != "en") {
			LOG(LogLevel::INFO) << "Could not find or load " + Config::LANGUAGE + ".";
		}
	}

	// Telling Qt to translate its own internal widgets
	QTranslator qtTranslator;
	// "qt_" is a meta-catalog that includes base, widgets, gui, etc.
	if (qtTranslator.load("qt_" + Config::LANGUAGE, QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
		app.installTranslator(&qtTranslator);
	} else {
		// Fallback: If "qt_ro" isn't found, try "qtbase_ro"
		if(qtTranslator.load("qtbase_" + Config::LANGUAGE, QLibraryInfo::path(QLibraryInfo::TranslationsPath))){
			app.installTranslator(&qtTranslator);
		}
	}

	QString arg1 = QString(argv[1]);
	LOG(LogLevel::INFO) << "arg1:" << arg1;

	QString mode;
	QString destDir;
	OperationMode op;

	// Improved Argument Parsing
	if (argc > 1) {
		QString arg1 = QString(argv[1]);
		if (arg1 == "--settings") {
			Settings *dlg = new Settings(nullptr);
			dlg->setAttribute(Qt::WA_DeleteOnClose); // Clean up memory when closed
			dlg->show();
			return app.exec();
		} else if (arg1 == "cp" || arg1 == "mv") {
			mode = arg1;
			if (argc > 2)
				destDir = QString(argv[2]);
		} else if (arg1 == "help" || arg1 == "--help") {
			cout << "Usage: " << "Copy contents to clipboard" << endl;
			cout << "       " << APP_NAME << " [cp|mv] [dest dir]" << endl;
			cout << "       " << APP_NAME << " --settings" << endl;
			cout << "       " << APP_NAME << " --paste-to [dest dir]" << endl;
			return 0;
		}

		if (argc > 2 && arg1 == "--paste-to") {
			destDir = argv[2];
		}
	} else {
		cout << "No arguments provided." << endl;
		return 1;
	}


	// 1. Create a dummy invisible window
	// We use Qt::WindowTransparentForInput and make it tiny
	QWidget dummy;
	dummy.setWindowOpacity(0.01); // Effectively invisible
	dummy.setFixedSize(1, 1);
	dummy.show();
	dummy.raise();
	dummy.activateWindow();

	// 2. Local event loop to wait for Wayland to "seat" the window
	QEventLoop loop;
	QTimer::singleShot(200, &loop, &QEventLoop::quit);
	loop.exec();


	// Get Clipboard Data
	// URI Lists vs. Plain Text:
	// Standard Copy: When you Ctrl+C a file, Dolphin populates the clipboard with text/uri-list.
	// hasUrls() check looks specifically for this format.
	// Copy Location: This action usually populates the clipboard as text/plain (raw string text). 
	// Since a raw string is not technically a "URL list," mimeData->hasUrls() returns false.
	std::vector<std::string> sourceFiles;
	if (Config::DRY_RUN == false) {
		const QClipboard *clipboard = QApplication::clipboard();
		const QMimeData *mimeData = clipboard->mimeData();

		LOG(LogLevel::DEBUG) << "Available Formats:" << mimeData->formats().join(", ").toStdString();
		LOG(LogLevel::DEBUG) << "Clipboard Text:" << clipboard->text();
		// Check for the standard URI list first
		if (mimeData->hasFormat("text/uri-list")) {
			// ... logic for hasUrls()
			LOG(LogLevel::DEBUG) << "Has format text/uri-list";
		}

		if (mimeData->hasUrls()) {
			QList<QUrl> urlList = mimeData->urls();
			for (const QUrl &url : urlList) {

				LOG(LogLevel::DEBUG) << "url:" << url;

				if (url.isLocalFile()) {
					sourceFiles.push_back(url.toLocalFile().toStdString());
				}
			}

		} else if (mimeData->hasText()) {
			// Fallback for 'Copy Location' (Plain Text)
			QString rawText = mimeData->text();

			// Split by newlines in case multiple locations were copied
			QStringList lines = rawText.split(QRegularExpression("[\r\n]+"), Qt::SkipEmptyParts);

			for (const QString &line : lines) {
				QString trimmed = line.trimmed();

				// Remove 'file://' prefix if Dolphin included it in the text
				if (trimmed.startsWith("file://")) {
					trimmed = QUrl(trimmed).toLocalFile();
				}

				if (QFile::exists(trimmed)) {
					sourceFiles.push_back(trimmed.toStdString());
				}
			}

		} else {
				LOG(LogLevel::DEBUG) << "No clipboard data found.";
			}

		if (sourceFiles.empty()) {
			QMessageBox::warning(nullptr, "Error", 
				QCoreApplication::translate("Main", "No files found in clipboard!"));
			return 1;
		}
	}

	if (destDir.isEmpty()) {
		LOG(LogLevel::DEBUG) << "No destination directory provided.";
		QMessageBox::warning(nullptr, "Error", 
			QCoreApplication::translate("Main", "No destination directory provided!"));
		return 1;
	}

	QDir dest(destDir);
	if (!dest.exists()) {
		LOG(LogLevel::DEBUG) << "Destination directory does not exist:" << destDir;
		QMessageBox::warning(nullptr, "Error", 
			QCoreApplication::translate("Main", "Destination directory does not exist!"));
		return 1;
	}

	ClipboardAction action = detectClipboardAction();
	op = (action == ClipboardAction::Move) ? OperationMode::Move : OperationMode::Copy;
	QString actionString = (action == ClipboardAction::Move) ? "Move" : "Copy";
	LOG(LogLevel::DEBUG) << "Action from clipboard:" << actionString;

	if (arg1 == "cp")
		op = OperationMode::Copy;
	else if (arg1 == "mv")
		op = OperationMode::Move;

	// 4. If files exist, close dummy and show real window
	dummy.hide();
	MainWindow w(op, sourceFiles, destDir.toStdString());
	w.show();
	w.raise(); // Move window to top of stack
	w.activateWindow(); // Request keyboard/clipboard focus

	return app.exec();
}
