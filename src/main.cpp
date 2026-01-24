#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QLibraryInfo>
#include <QMessageBox>
#include <QMimeData>
#include <QTranslator>
#include <QUrl>
#include <QLockFile>
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

int main(int argc, char *argv[]) {
	QApplication app(argc, argv);

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
	QString tr_path = ":/translations/Movero_" + Config::LANGUAGE + ".qm";
	if (translator.load(tr_path)) {
		if (QCoreApplication::installTranslator(&translator)) {
			LOG(LogLevel::INFO) << "Successfully installed " + Config::LANGUAGE + " translation.";
		} else {
			LOG(LogLevel::INFO) << "Failed to install translator.";
		}
	} else {
		LOG(LogLevel::INFO) << "Could not find or load " + Config::LANGUAGE + ".";
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

	LOG(LogLevel::INFO) << APP_NAME << "started.";
	LOG(LogLevel::INFO) << "Version" << APP_VERSION;
	QString arg1 = QString(argv[1]);
	LOG(LogLevel::INFO) << "arg1: " << arg1;

	QString mode;
	QString destDir;
	OperationMode op;

	// Improved Argument Parsing
	if (argc > 1) {
		QString arg1 = QString(argv[1]);
		if (arg1 == "settings") {
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
			cout << "       " << APP_NAME << " settings" << endl;
			return 0;
		} else {
			cout << "Unknown argument: " << arg1.toStdString() << endl;
			return 1;
		}
	} else {
		cout << "No arguments provided." << endl;
		return 1;
	}

	std::vector<std::string> sourceFiles;
	if (Config::DRY_RUN == false) {
		// Get Clipboard Data
		const QClipboard *clipboard = QApplication::clipboard();
		const QMimeData *mimeData = clipboard->mimeData();

		if (mimeData->hasUrls()) {
			QList<QUrl> urlList = mimeData->urls();
			for (const QUrl &url : urlList) {
				if (url.isLocalFile()) {
					sourceFiles.push_back(url.toLocalFile().toStdString());
				}
			}
		} else {
			LOG(LogLevel::DEBUG) << "No clipboard data found.";
		}

		if (sourceFiles.empty()) {
			QMessageBox::warning(nullptr, "Error", "No files found in clipboard!");
			return 1;
		}
	}

	if (destDir.isEmpty()) {
		LOG(LogLevel::DEBUG) << "No destination directory provided.";
		QMessageBox::warning(nullptr, "Error", "No destination directory provided!");
		return 1;
	}

	QDir dest(destDir);
	if (!dest.exists()) {
		LOG(LogLevel::DEBUG) << "Destination directory does not exist:" << destDir;
		QMessageBox::warning(nullptr, "Error", "Destination directory does not exist!");
		return 1;
	}

	if (arg1 == "cp")
		op = OperationMode::Copy;
	else if (arg1 == "mv")
		op = OperationMode::Move;

	MainWindow w(op, sourceFiles, destDir.toStdString());
	w.show();

	return app.exec();
}
