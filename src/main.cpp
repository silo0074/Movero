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
#include "StartupHandler.h"

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

// What it finds: Leaks (memory allocated but never freed) 
// and "Heuristic" errors (writing past the end of an array).
// valgrind --leak-check=full ./Movero

bool isXcbPluginAvailable() {
	// Get the path where Qt expects platform plugins
	// On Linux, this is usually /usr/lib/qt/plugins/platforms
	QString pluginPath = QLibraryInfo::path(QLibraryInfo::PluginsPath) + "/platforms";

	// The filename for the X11/XCB plugin
	QString xcbPlugin = pluginPath + "/libqxcb.so";

	// Check if the file exists and is readable
	return QFile::exists(xcbPlugin);
}


int main(int argc, char *argv[]) {
	// Check if we are on a Wayland session
	// Using XCB plugin to allow window positioning which is not possible on Wayland.
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

	// Initialize the logging system
	LogManager::init();

	QApplication app(argc, argv);
	app.setOrganizationName(APP_NAME);
	app.setApplicationName(APP_NAME);
	app.setDesktopFileName(QString(APP_NAME) + ".desktop");

	// Load user settings
	// Must be done after setOrganizationName
	Config::load();

	LOG(LogLevel::INFO) << APP_NAME << "started.";
	LOG(LogLevel::INFO) << "Version" << APP_VERSION;
	if (Config::DRY_RUN) {
		LOG(LogLevel::INFO) << "Using dry run mode.";
	}

	// Set global stylesheet
	QFile styleFile(":/style.qss");
	if (styleFile.open(QFile::ReadOnly)) {
		QString styleSheet = QLatin1String(styleFile.readAll());
		qApp->setStyleSheet(styleSheet);
	}

	// Set translator
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

	// Create a dummy invisible window
	// We use Qt::WindowTransparentForInput and make it tiny
	// QWidget dummy;
	// dummy.setWindowOpacity(0.01); // Effectively invisible
	// dummy.setFixedSize(1, 1);
	// dummy.show();
	// dummy.raise();
	// dummy.activateWindow();

	// Local event loop to wait for Wayland to "seat" the window
	QEventLoop loop;
	QTimer::singleShot(200, &loop, &QEventLoop::quit);
	loop.exec();

	// Parse arguments and clipboard
	StartupOptions options = StartupHandler::parse(app.arguments());

	// Display Settings
	if (options.showSettings) {
		Settings *dlg = new Settings(nullptr);
		dlg->setAttribute(Qt::WA_DeleteOnClose); // Clean up memory when closed
		dlg->show();
		return app.exec();
	}

	// Display Help in the terminal
	if (options.showHelp) {
		cout << "Usage: " << "Copy contents to clipboard" << endl;
		cout << "       " << APP_NAME << " [cp|mv] [dest dir]" << endl;
		cout << "       " << APP_NAME << " --settings" << endl;
		cout << "       " << APP_NAME << " --paste-to [dest dir]" << endl;
		return 0;
	}

	if (!options.valid) {
		if (options.errorMessage == "No arguments provided.") {
			cout << "No arguments provided." << endl;
		} else {
			QMessageBox::warning(nullptr, "Error", options.errorMessage);
		}
		return 1;
	}

	// Create a temporary path for the lock file.
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
		QMessageBox::warning(
			nullptr,
			QCoreApplication::translate("Main", "Already Running"),
			QString::fromStdString(APP_NAME) + QCoreApplication::translate("Main", " is already running. Please close the other instance first."));
		return 0;
	}

	// Close dummy and show real window
	// dummy.hide();

	LOG(LogLevel::DEBUG) << "options.mode" << static_cast<int>(options.mode);
	LOG(LogLevel::DEBUG) << "options.sources" << options.sources;

	MainWindow w(options.mode, options.sources, options.dest);
	w.show();
	w.raise(); // Move window to top of stack
	w.activateWindow(); // Request keyboard/clipboard focus

	return app.exec();
}
