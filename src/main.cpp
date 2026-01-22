#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QMessageBox>
#include <QMimeData>
#include <QUrl>
#include <iostream>

#include "Config.h"
#include "LogHelper.h"
#include "MainWindow.h"
#include "Settings.h"

using std::cout;
using std::endl;

// OpenSuse is my operating system
// sudo zypper install CMake gcc-c++ mold lld xxhash-devel \
// qt6-base-devel qt6-widgets-devel

// Ubuntu
// sudo apt update
// sudo apt install cmake g++ mold lld libxxhash-dev \
// qt6-base-dev qt6-base-dev-tools

// # From your project root:
// mkdir build && cd build
// cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=mold" ..
// make -j$(nproc)

int main(int argc, char *argv[]) {
	QApplication app(argc, argv);

	// Set global stylesheet
	QFile styleFile(":/style.qss");
	if (styleFile.open(QFile::ReadOnly)) {
		QString styleSheet = QLatin1String(styleFile.readAll());
		qApp->setStyleSheet(styleSheet);
	}

	// app.setOrganizationName(APP_NAME);
	app.setApplicationName(APP_NAME);
	app.setDesktopFileName(QString(APP_NAME) + ".desktop");

	Config::load();

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
