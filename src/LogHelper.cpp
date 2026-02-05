#include "LogHelper.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QMutex>
#include <QStandardPaths>
#include <QTextStream>
#include <unistd.h> // Required for isatty
#include <queue>
#include <QWaitCondition>
#include <QThread>
#include <atomic>

static QFile logFile;
static QTextStream logStream;
/*
logMutex:
	This ensures that only one piece of code
	can access the logStream or resize the file (via LogManager::clear()) at a time.
	If you called LogManager::clear() from the main UI thread
	while the LogWorker was in the middle of writing a long string,
	the file pointer could become invalid, resulting in garbled text or an application crash.

Why use QMutexLocker instead of .lock()?
	Using the locker object is much safer than manually calling mutex.lock() 
	and mutex.unlock() for two main reasons:
Exception Safety:
	If an error occurs or a function returns early, 
	the QMutexLocker is destroyed automatically, releasing the lock. 
	If you forgot to call .unlock() manually, your app would "deadlock" (freeze forever).
Readability:
	It makes it very clear which block of code is "thread-safe" 
	just by looking at the curly braces { }.
*/
static QMutex logMutex;
const qint64 MAX_LOG_SIZE = 5 * 1024 * 1024; // 5 MB

// Thread-Safe Queue: 
// protected by a QMutex to store log messages temporarily.
static std::queue<QString> logQueue;
static QMutex queueMutex;
static QWaitCondition queueCondition;
static std::atomic<bool> isLoggingRunning(false);


/*--------------------------------------------------------------------------
Background Worker Thread: LogWorker class (inheriting from QThread) 
that runs at low priority. It waits for messages in the queue 
and writes them to the disk.
----------------------------------------------------------------------------*/
class LogWorker : public QThread {
public:
	void run() override {
		setPriority(QThread::LowPriority);
		while (isLoggingRunning || !isQueueEmpty()) {
			QString msg;
			{
				// Without this lock, if two threads tried to .push() a log message 
				// at the exact same millisecond, the internal memory of the std::queue 
				// could become corrupted, leading to a crash (Segmentation Fault).
				QMutexLocker locker(&queueMutex);
				while (logQueue.empty() && isLoggingRunning) {
					// The locker works with the WaitCondition to let the background 
					// thread "sleep" without consuming CPU power until a new log message arrives.
					queueCondition.wait(&queueMutex);
				}
				if (!logQueue.empty()) {
					msg = logQueue.front();
					logQueue.pop();
				}
			}

			if (!msg.isEmpty()) {
				QMutexLocker locker(&logMutex);
				if (logFile.isOpen()) {
					logStream << msg;
					logStream.flush();
				}
			}
		}
	}

private:
	bool isQueueEmpty() {
		QMutexLocker locker(&queueMutex);
		return logQueue.empty();
	}
};

static LogWorker* logThread = nullptr;

void cleanupLogging() {
	LOG(LogLevel::INFO) << "Closing logging system.";
	isLoggingRunning = false;
	queueCondition.wakeAll();
	if (logThread) {
		logThread->wait();
		delete logThread;
		logThread = nullptr;
	}
}

// The custom message handler that Qt will call for all qInfo, qDebug, etc.
void logMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg) {
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

	// Only output to stderr if we are actually in a terminal
    if (isatty(STDERR_FILENO)) {
		fprintf(stderr, "%s", formattedMsg.toLocal8Bit().constData());
	}

	// Push formatted strings to the queue instead of writing to the file directly. 
	// This removes the file I/O overhead from the calling thread.
	/*
		Compound Statements or Explicit Scopes. 
		They are used here specifically to control the lifetime of the QMutexLocker.
		A QMutexLocker stays locked as long as the object exists. 
		It is destroyed (and the mutex is unlocked) the moment the code execution 
		hits the closing brace }.
	*/
	{
		QMutexLocker locker(&queueMutex);
		logQueue.push(formattedMsg);
	}
	queueCondition.wakeOne();
}

namespace LogManager {
	QString getLogFilePath() {
		return logFile.fileName(); 
	}

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
		qInfo() << "Log path set to:" << logPath;

		// Rotate log if it's too big
		if (logFile.exists() && logFile.size() > MAX_LOG_SIZE) {
			QFile::remove(logPath + ".1"); // Remove old backup
			logFile.rename(logPath + ".1"); // Rename current to backup
			logFile.setFileName(logPath); // Point back to the original name
		}

		if (logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
			logStream.setDevice(&logFile);
			
			isLoggingRunning = true;
			logThread = new LogWorker();
			logThread->start();
			// Cleanup routine registered via qAddPostRoutine to ensure all pending logs 
			// are flushed and the thread exits gracefully when the application closes.
			qAddPostRoutine(cleanupLogging);

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
