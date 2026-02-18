#ifndef DEBUGHELPER_H
#define DEBUGHELPER_H

#include <QFile>
#include <QString>
#include <QDebug>

// Define log levels
enum class LogLevel {
    INFO,
    DEBUG,
    WARNING,
    ERROR
};

namespace LogManager {
	void init();
	QString getLogFilePath();
	void clear();
}

// The LOG macro maps to the standard Qt logging functions.
// The custom message handler (in LogHelper.cpp) will format the output.
#undef LOG
#define LOG(level) ( \
	(level == LogLevel::DEBUG)   ? QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug().noquote() :     \
	(level == LogLevel::INFO)    ? QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info().noquote() :      \
	(level == LogLevel::WARNING) ? QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning().noquote() :   \
	(level == LogLevel::ERROR)   ? QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).critical().noquote() :  \
								   QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug().noquote()       \
)

/*
Usage example:
#include "LogHelper.h"
void someFunction(const QUrl& url, const QStringList& files) {
    int count = 5;
    
    // Logging a single variable
    LOG(LogLevel::DEBUG) << "Counter value:" << count;

    // Logging a QUrl
    LOG(LogLevel::INFO) << "Processing URL:" << url;

    // Logging a List (QStringList or QList)
    LOG(LogLevel::INFO) << "File list:" << files;

    // Chaining multiple different types together
    LOG(LogLevel::ERROR) << "Failed to process" << url << "after" << count << "attempts.";
}
*/

#endif // DEBUGHELPER_H
