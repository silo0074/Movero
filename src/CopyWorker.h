#pragma once

#include <QThread>
#include <QObject>
#include <QMutex>
#include <QWaitCondition>
#include <vector>
#include <filesystem>
#include <atomic>

#include "Config.h"


class CopyWorker : public QThread {
    Q_OBJECT

public:
    enum Mode { Copy, Move };
    struct FileError {
        QString path;
        QString errorMsg;
    };
    
    enum ConflictAction {
        Replace,
        Skip,
        Rename,
        Cancel
    };

    CopyWorker(const std::vector<std::string>& sources, const std::string& destDir, Mode mode, QObject* parent = nullptr);

    void pause();
    void resume();
    void cancel();
    void resolveConflict(ConflictAction action, bool applyToAll);

signals:
    void progressChanged(QString file, int percent, double curSpeed, double avgSpeed, QString eta);
    void statusChanged(QString status);
    void totalProgress(int fileCount, int totalFiles);
    void finished();
    void errorOccurred(FileError error);
    void conflictNeeded(QString src, QString dest);

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
    ConflictAction m_savedAction = Replace;

    std::chrono::steady_clock::time_point m_overallStartTime;
    std::chrono::duration<double> m_totalPausedDuration{0};
    uintmax_t m_totalSizeToCopy = 0; // Calculated during Scan Phase
    uintmax_t m_totalWorkBytes = 0; // (Size of all files * 2)
    uintmax_t m_totalBytesProcessed = 0; // Global counter

    struct CopyTask {
        std::filesystem::path src;
        std::filesystem::path dest;
    };

    // Buffer size: 1MB is a good balance for modern NVMe
    const size_t BUFFER_SIZE = Config::BUFFER_SIZE;
    
    bool copyFile(const std::filesystem::path& src, const std::filesystem::path& dest);
    bool verifyFile(const std::filesystem::path& path, uint64_t expectedHash);
    void updateProgress(const std::filesystem::path& path, qint64 totalRead, qint64 fileSize, 
                        qint64& lastBytesRead, std::chrono::steady_clock::time_point& lastSampleTime);
};
