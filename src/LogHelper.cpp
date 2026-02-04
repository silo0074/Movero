#include "LogHelper.h"
#include <QDateTime>
#include <QDir>
#include <QMutex>
#include <QStandardPaths>
#include <QTextStream>

static QFile logFile;
static QTextStream logStream;
static QMutex logMutex;
const qint64 MAX_LOG_SIZE = 5 * 1024 * 1024; // 5 MB

// The custom message handler that Qt will call for all qInfo, qDebug, etc.
void logMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg) {
	QMutexLocker locker(&logMutex);

	QString level;
	switch (type) {
	case QtDebugMsg: level = "DEBUG"; break;
	case QtInfoMsg: level = "INFO"; break;
	case QtWarningMsg: level = "WARNING"; break;
	case QtCriticalMsg: level = "CRITICAL"; break;
	case QtFatalMsg: level = "FATAL"; break;
	}

	const char *fileName = context.file ? strrchr(context.file, '/') : nullptr;
	if (fileName) fileName++;
	else fileName = context.file;

	// Format: YYYY-MM-DD HH:MM:SS.ms [LEVEL] (file:line) message
	QString formattedMsg =
		QString("%1 [%2] (%3:%4) %5\n")
			.arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz"))
			.arg(level)
			.arg(fileName ? fileName : "")
			.arg(context.line)
			.arg(msg);

	// Always output the formatted message to the console.
	fprintf(stderr, "%s", formattedMsg.toLocal8Bit().constData());

	// If the log file is open, also write the message to it.
	if (logFile.isOpen()) {
		logStream << formattedMsg;
		logStream.flush(); // Ensure it's written immediately
	}
}

namespace LogManager {

QString getLogFilePath() { return logFile.fileName(); }

void init() {
	QString logDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
	if (logDir.isEmpty()) {
		qWarning() << "Could not find writable location for logs.";
		return;
	}
	QDir dir(logDir);
	if (!dir.exists()) {
		dir.mkpath(".");
	}

	QString logPath = logDir + "/Movero.log";
	logFile.setFileName(logPath);

	// Rotate log if it's too big
	if (logFile.exists() && logFile.size() > MAX_LOG_SIZE) {
		QFile::remove(logPath + ".1"); // Remove old backup
		logFile.rename(logPath + ".1"); // Rename current to backup
		logFile.setFileName(logPath); // Point back to the original name
	}

	if (logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
		logStream.setDevice(&logFile);
		qInstallMessageHandler(logMessageHandler);
	} else {
		qWarning() << "Failed to open log file:" << logPath;
	}
}

void clear() {
	QMutexLocker locker(&logMutex);
	if (logFile.isOpen()) {
		logFile.resize(0);
	}
}

} // namespace LogManager
