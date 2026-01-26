#pragma once

#include <QMutex>
#include <QObject>
#include <QThread>
#include <QWaitCondition>
#include <atomic>
#include <filesystem>
#include <vector>

#include "Config.h"

class CopyWorker : public QThread
{
	Q_OBJECT

public:
	enum Mode { Copy,
				Move };

	enum ErrorType {
		NoError,
		DiskFull,
		DriveCheckFailed,
		SourceOpenFailed,
		FileOpenFailed,
		ReadError,
		UnexpectedEOF,
		WriteError,
		ChecksumMismatch,
		DestinationIsDirectory
	};
	Q_ENUM(ErrorType)

	struct FileError
	{
		ErrorType code;
		QString path;
		QString extraInfo;
	};

	enum ConflictAction {
		Replace,
		Skip,
		Rename,
		Cancel
	};

	enum Status {
		DryRunGenerating,
		Scanning,
		RemovingEmptyFolders,
		Copying,
		GeneratingHash,
		Verifying
	};
	Q_ENUM(Status)

	std::atomic<uintmax_t> m_totalSizeToCopy{0}; // Calculated during Scan Phase
	std::atomic<uintmax_t> m_completedFilesSize{0}; // Size of files fully processed
	std::atomic<uintmax_t> m_totalBytesCopied{0}; // Total bytes written to disk (including partial)
	CopyWorker(const std::vector<std::string> &sources, const std::string &destDir, Mode mode, QObject *parent = nullptr);

	void pause();
	void resume();
	void cancel();
	void resolveConflict(ConflictAction action, bool applyToAll, QString newName = "");

signals:
	void progressChanged(QString src, QString dest, int percent, int totalPercent, double curSpeed, double avgSpeed, long secondsLeft);
	void statusChanged(Status status);
	void totalProgress(int fileCount, int totalFiles);
	void finished();
	void errorOccurred(FileError error);
	void conflictNeeded(QString src, QString dest, QString suggestedName);
	void fileCompleted(QString path, QString srcHash, QString destHash);

protected:
	void run() override;

private:
	std::vector<std::string> m_sources;
	std::string m_destDir;
	Mode m_mode;
	QMutex m_sync;
	QWaitCondition m_pauseCond;
	std::atomic<bool> m_paused;
	std::atomic<bool> m_cancelled;

	// Conflict Resolution
	QMutex m_inputMutex;
	QWaitCondition m_inputWait;
	ConflictAction m_userAction;
	bool m_applyAll = false;
	bool m_waitingForUser = false;
	ConflictAction m_savedAction = Replace;
	QString m_userNewName;

	std::chrono::steady_clock::time_point m_overallStartTime;
	std::chrono::duration<double> m_totalPausedDuration{0};
	uintmax_t m_totalWorkBytes = 0; // (Size of all files * 2)
	uintmax_t m_totalBytesProcessed = 0; // Global counter

	struct CopyTask
	{
		std::filesystem::path src;
		std::filesystem::path dest;
	};

	// Buffer size: 1MB is a good balance for modern NVMe
	const size_t BUFFER_SIZE = Config::BUFFER_SIZE;

	bool copyFile(const std::filesystem::path &src, const std::filesystem::path &dest);
	bool verifyFile(const std::filesystem::path &src, const std::filesystem::path &dest, uint64_t expectedHash, uint64_t &diskHash);
	void updateProgress(const std::filesystem::path &src, const std::filesystem::path &dest, qint64 totalRead, qint64 fileSize, qint64 &lastBytesRead, std::chrono::steady_clock::time_point &lastSampleTime);
};
