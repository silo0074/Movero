#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QUrl>
#include <QMessageBox>
#include <QDir>
#include <iostream>

#include "MainWindow.h"
#include "Config.h"
#include "LogHelper.h"

using std::cout;
using std::endl;

// OpenSuse
// sudo zypper install cmake gcc-c++ mold lld xxhash-devel \
qt6-base-devel qt6-widgets-devel

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

    LOG(LogLevel::INFO) << APP_NAME << "started.";
    LOG(LogLevel::INFO) << "Version" << APP_VERSION;

    // Arguments: FastCopier [cp|mv]
    QString mode = "cp";
    if (argc > 1) mode = QString(argv[1]);

    if (mode == "help") {
        cout << "Version " << APP_VERSION << endl;
        cout << "Usage: " << APP_NAME << " [cp|mv] [dest dir]" << endl;
        return 0;
    }
    
    if ((mode != "cp") && (mode != "mv")) {
        cout << "Incorrect arguments." << endl;
        cout << "Usage: " << APP_NAME << " [cp|mv] [dest dir]" << endl;
        return 1;
    }

    std::vector<std::string> sourceFiles;
    if(Config::DRY_RUN == false){
        // Get Clipboard Data
        const QClipboard *clipboard = QApplication::clipboard();
        const QMimeData *mimeData = clipboard->mimeData();

        if (mimeData->hasUrls()) {
            QList<QUrl> urlList = mimeData->urls();
            for (const QUrl& url : urlList) {
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

    // Prompt user for destination if not provided (Simplification: Use CWD or Picker)
    // For Service Menus, the destination is usually the folder you right-clicked IN.
    // However, Dolphin usually passes the selected files as args, not the destination.
    // Assuming the app is triggered inside the destination folder, we use CWD.
    // Alternatively, we open a FileDialog to pick destination.

    // For this implementation, we assume the user right-clicked "Paste here" equivalent.
    std::string destDir = QDir::currentPath().toStdString();

    // If triggered via arguments, you might pass dest as argv[2]
    if (argc > 2) destDir = argv[2];

    MainWindow w(mode, sourceFiles, destDir);
    w.show();

    return app.exec();
}
