#pragma once

#include "MainWindow.h" // For OperationMode
#include <QCoreApplication>
#include <QString>
#include <QStringList>
#include <string>
#include <vector>

struct StartupOptions {
	OperationMode mode = OperationMode::Copy;
	std::vector<std::string> sources;
	std::string dest;
	bool showSettings = false;
	bool showHelp = false;
	bool valid = false;
	QString errorMessage;
};

class StartupHandler {
	Q_DECLARE_TR_FUNCTIONS(StartupHandler)
public:
	static StartupOptions parse(const QStringList &args);

private:
	enum class ClipboardAction {
		Copy,
		Move
	};
	static ClipboardAction detectClipboardAction();
};
