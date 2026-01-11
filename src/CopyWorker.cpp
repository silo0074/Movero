// #include <fstream>
// #include <iostream>
#include <xxhash.h>
#include <fcntl.h>
#include <unistd.h>
#include <QDateTime>
#include <memory>
#include <cstdlib>

#include "CopyWorker.h"
#include "Config.h"

namespace fs = std::filesystem;

CopyWorker::CopyWorker(const std::vector<std::string>& sources, const std::string& destDir, Mode mode, QObject* parent)
: QThread(parent), m_sources(sources), m_destDir(destDir), m_mode(mode), m_paused(false), m_cancelled(false) {}

void CopyWorker::pause() {
    m_paused = true;
}

void CopyWorker::resume() {
    QMutexLocker locker(&m_sync);
    m_paused = false;
    m_pauseCond.wakeAll();
}

void CopyWorker::cancel() {
    m_cancelled = true;
    resume(); // Break wait if paused
}


void CopyWorker::updateProgress(const fs::path& path, qint64 fileRead, qint64 fileSize, 
                                qint64& lastBytesRead, std::chrono::steady_clock::time_point& lastSampleTime) {
    auto now = std::chrono::steady_clock::now();
    std::chrono::duration<double> elapsedSinceLast = now - lastSampleTime;

    if (elapsedSinceLast.count() < Config::SPEED_UPDATE_INTERVAL) return;

    // 1. Current File Progress
    int filePercent = (int)((fileRead * 100) / fileSize);

    // 2. Global Progress (Total bytes including Copy + Verify)
    // m_totalWorkBytes is (Total Size * 2)
    int totalPercent = (int)((m_totalBytesProcessed * 100) / m_totalWorkBytes);

    // 3. Average Speed & ETA
    std::chrono::duration<double> totalActiveTime = (now - m_overallStartTime) - m_totalPausedDuration;
    double avgMbps = (m_totalBytesProcessed / (1024.0 * 1024.0)) / (totalActiveTime.count() + 0.001);

    uintmax_t bytesLeft = m_totalWorkBytes - m_totalBytesProcessed;
    QString etaStr = "Calculating...";

    if (avgMbps > 0.5) {
        int secondsLeft = static_cast<int>((bytesLeft / (1024.0 * 1024.0)) / avgMbps);
        
        if (secondsLeft < 60) etaStr = QString("%1s").arg(secondsLeft);
        else etaStr = QString("%1m %2s").arg(secondsLeft / 60).arg(secondsLeft % 60);
    }

    // Pass the instantaneous speed for the graph, and avg/eta for the labels
    double curMbps = ((fileRead - lastBytesRead) / (1024.0 * 1024.0)) / elapsedSinceLast.count();
    
    emit progressChanged(QString::fromStdString(path.filename().string()), 
                        filePercent, curMbps, avgMbps, etaStr);

    lastSampleTime = now;
    lastBytesRead = fileRead;
}


void CopyWorker::run() {
    std::vector<CopyTask> tasks;
    std::vector<fs::path> sourceDirs; // To clean up empty folders in Move mode
    uintmax_t totalBytesRequired = 0;
    
    if (Config::DRY_RUN) {
        // Simulate a 2GB file task
        totalBytesRequired = Config::DRY_RUN_FILE_SIZE; 
        tasks.push_back({"DRY_RUN_SOURCE", fs::path(m_destDir) / "DRY_RUN_2GB.dat"});
        emit statusChanged("DRY RUN: Generating test file...");
    } else {
        
        // PHASE 1: Scan, Map, and Calculate Size
        emit statusChanged("Scanning and calculating space...");
        for (const auto& srcStr : m_sources) {
            fs::path srcRoot(srcStr);
            if (!fs::exists(srcRoot)) continue;
            
            fs::path base = srcRoot.parent_path();
            
            if (fs::is_directory(srcRoot)) {
                sourceDirs.push_back(srcRoot);
                for (const auto& entry : fs::recursive_directory_iterator(srcRoot)) {
                    if (m_cancelled) return;
                    if (entry.is_directory()) {
                        sourceDirs.push_back(entry.path());
                    } else if (entry.is_regular_file()) {
                        totalBytesRequired += entry.file_size();
                        fs::path rel = fs::relative(entry.path(), base);
                        tasks.push_back({entry.path(), fs::path(m_destDir) / rel});
                    }
                }
            } else {
                totalBytesRequired += fs::file_size(srcRoot);
                fs::path rel = fs::relative(srcRoot, base);
                tasks.push_back({srcRoot, fs::path(m_destDir) / rel});
            }
        }
    }
    
    
    // PHASE 1.5: Verify Available Space
    try {
        fs::space_info destSpace = fs::space(m_destDir);
        
        // Add a 50MB safety margin to account for filesystem overhead/metadata
        uintmax_t safetyMargin = 50 * 1024 * 1024; 
        
        if (destSpace.available < (totalBytesRequired + safetyMargin)) {
            double reqGB = totalBytesRequired / (1024.0 * 1024.0 * 1024.0);
            double availGB = destSpace.available / (1024.0 * 1024.0 * 1024.0);
            
            emit errorOccurred({
                "Disk Space Error", 
                QString("Not enough space. Required: %1 GB, Available: %2 GB")
                .arg(reqGB, 0, 'f', 2)
                .arg(availGB, 0, 'f', 2)
            });
            return; // Terminate before starting
        }
    } catch (const fs::filesystem_error& e) {
        emit errorOccurred({"Drive Error", "Could not determine available space on destination."});
        return;
    }
    
    
    // PHASE 2: Execute Tasks
    int totalFiles = tasks.size();
    int processed = 0;

    m_overallStartTime = std::chrono::steady_clock::now();
    m_totalPausedDuration = std::chrono::duration<double>::zero();
    m_totalBytesProcessed = 0;
    m_totalSizeToCopy = totalBytesRequired;
    m_totalWorkBytes = totalBytesRequired * 2; // Copying + Verifying
    m_overallStartTime = std::chrono::steady_clock::now();
    
    for (const auto& task : tasks) {
        if (m_cancelled) break;
        fs::create_directories(task.dest.parent_path());

        // copyFile returns true ONLY if verification succeeds
        if (copyFile(task.src, task.dest)) { // copyFile verifies checksum
            if (m_mode == Move && !Config::DRY_RUN) {
                fs::remove(task.src); 
            }
        }
        processed++;
        emit totalProgress(processed, totalFiles);
    }


    // PHASE 3: Cleanup (Move Mode Only)
    // We only reach this if we are moving folders
    if (m_mode == Move && !m_cancelled) {
        emit statusChanged("Removing empty folders...");
        
        // Sort by length descending: ensures /A/B/C is deleted before /A/B/
        std::sort(sourceDirs.begin(), sourceDirs.end(), [](const fs::path& a, const fs::path& b) {
            return a.string().length() > b.string().length();
        });

        for (const auto& dir : sourceDirs) {
            try {
                if (fs::exists(dir) && fs::is_directory(dir) && fs::is_empty(dir)) {
                    fs::remove(dir);
                }
            } catch (...) { /* Skip folders that still have files due to errors */ }
        }
    }

    emit finished();
}


bool CopyWorker::copyFile(const fs::path& src, const fs::path& dest) {
    int fd_in = -1;

    // Open the source file only if not in dry run mode
    if (!Config::DRY_RUN) {
        // Low-level I/O for speed and control
        fd_in = open(src.c_str(), O_RDONLY);

        if (fd_in < 0) {
            emit errorOccurred({QString::fromStdString(src.string()), "Failed to open source"});
            return false;
        }

        // Hint kernel: We will read this once, sequentially. Don't cache aggressively.
        posix_fadvise(fd_in, 0, 0, POSIX_FADV_SEQUENTIAL | POSIX_FADV_NOREUSE);
    }

    // Using O_SYNC to ensure metadata is flushed, but keeping standard I/O for write speed
    int fd_out = open(dest.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    // int fd_out = open(dest.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_SYNC, 0644);
    
    if ((!Config::DRY_RUN && fd_in < 0) || fd_out < 0) {
        emit errorOccurred({QString::fromStdString(src.string()), "Failed to open file"});
        if(fd_in >= 0) close(fd_in);
        if(fd_out >= 0) close(fd_out);
        return false;
    }

    XXH64_state_t* hashState = XXH64_createState();
    XXH64_reset(hashState, 0);

    // Use aligned_alloc instead of std::vector for maximum performance
    const size_t ALIGNMENT = 4096; // Standard page size

    // Allocate memory
    void* rawPtr = std::aligned_alloc(ALIGNMENT, Config::BUFFER_SIZE);
    if (!rawPtr) return false;

    // Use unique_ptr with a custom deleter so it calls free() automatically
    std::unique_ptr<char, decltype(&std::free)> buffer(static_cast<char*>(rawPtr), std::free);

    // std::vector<char> buffer(BUFFER_SIZE, 0); // Initialize with zeros
    qint64 totalRead = 0;
    qint64 fileSize = Config::DRY_RUN ? (Config::DRY_RUN_FILE_SIZE) : fs::file_size(src);
    auto startTime = std::chrono::steady_clock::now();
    ssize_t bytesRead;

    auto lastSampleTime = startTime;
    qint64 lastBytesRead = 0;

    emit statusChanged("Copying..."); // Notify UI

    // Read source file and write to destination
    while (totalRead < fileSize) {
        if (m_cancelled) break;

        // Pause Logic
        if (m_paused) {
            auto pauseStart = std::chrono::steady_clock::now();
            QMutexLocker locker(&m_sync);
            m_pauseCond.wait(&m_sync);

            // Add the time spent paused to our offset
            auto pauseEnd = std::chrono::steady_clock::now();
            m_totalPausedDuration += (pauseEnd - pauseStart);

            // Reset the sampling clock so the pause duration isn't 
            // counted as "active time" in the next speed calculation.
            lastSampleTime = pauseEnd;
            lastBytesRead = totalRead;
        }

        size_t toRead = std::min((qint64)BUFFER_SIZE, fileSize - totalRead);
        ssize_t bytesRead;

        if (Config::DRY_RUN) {
            bytesRead = toRead; // Simulate reading from disk
            // Simulate disk latency (roughly 10ms for a mechanical seek/read)
            // This prevents the "instant" processing that causes GB/s spikes
            QThread::msleep(10);
        } else {
            bytesRead = read(fd_in, buffer.get(), toRead);
        }

        if (bytesRead <= 0) break;

        // Calculate Hash on the fly
        XXH64_update(hashState, buffer.get(), bytesRead);

        // Write
        ssize_t written = write(fd_out, buffer.get(), bytesRead);
        if (written != bytesRead) {
            emit errorOccurred({QString::fromStdString(src.string()), "Write error"});
            break;
        }

        totalRead += bytesRead;
        m_totalBytesProcessed += bytesRead;

        // Calculate and update speed every 500ms for a smooth, reactive graph
        updateProgress(src, totalRead, fileSize, lastBytesRead, lastSampleTime);
    }

    // Force 100% and reset speed graph after copying
    emit progressChanged(QString::fromStdString(src.filename().string()), 
                        100, 0.0, 0.0, "");

    // Hash the source file
    emit statusChanged("Generating Source Hash...");
    uint64_t srcHash = XXH64_digest(hashState);
    XXH64_freeState(hashState);
    if (fd_in >= 0) close(fd_in);
    
    // Ensure data is on disk before verification
    // fsync(fd_out);
    fdatasync(fd_out);
    close(fd_out);

    // Open the file again briefly just to invalidate the cache for it
    int fd_drop = open(dest.c_str(), O_RDONLY);
    if (fd_drop >= 0) {
        // Tell the OS: "I'm done with this, throw it out of RAM."
        posix_fadvise(fd_drop, 0, 0, POSIX_FADV_DONTNEED);
        close(fd_drop);
    }
    
    // Verify Phase (Read from disk)
    // This will also verify the test generated file against the generated hash
    return verifyFile(dest, srcHash);
}


bool CopyWorker::verifyFile(const fs::path& path, uint64_t expectedHash) {
    emit statusChanged("Verifying Checksum..."); // Update UI status

    // Open with O_DIRECT to bypass OS cache entirely
    int fd = open(path.c_str(), O_RDONLY);
    // int fd = open(path.c_str(), O_RDONLY | O_DIRECT);
    
    // Fallback: Some filesystems/OSs don't support O_DIRECT
    // For simplicity and compatibility, we use posix_fadvise(DONTNEED) to urge reading from disk
    // if (fd < 0) {
    //     fd = open(path.c_str(), O_RDONLY);
    //     posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
    // }

    // Hint that we are reading sequentially; the kernel will pre-fetch data into RAM 
    // in the background while XXH64 is processing the current buffer.
    posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL | POSIX_FADV_WILLNEED);

    if (fd < 0) return false;
    
    // Force the kernel to drop the page cache for this specific file.
    // This ensures that the subsequent read() calls fetch data from the HDD.
    posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);

    XXH64_state_t* state = XXH64_createState();
    XXH64_reset(state, 0);

    qint64 fileSize = fs::file_size(path);
    qint64 totalRead = 0;

    // Use aligned_alloc instead of std::vector for maximum performance
    const size_t ALIGNMENT = 4096; // Standard page size

    // Allocate memory
    void* rawPtr = std::aligned_alloc(ALIGNMENT, Config::BUFFER_SIZE);
    if (!rawPtr) return false;

    // Use unique_ptr with a custom deleter so it calls free() automatically
    std::unique_ptr<char, decltype(&std::free)> buffer(static_cast<char*>(rawPtr), std::free);

    // std::vector<char> buffer(BUFFER_SIZE);
    // ssize_t n;

    // Start the timer for verification speed
    auto startTime = std::chrono::steady_clock::now();
    auto lastSampleTime = startTime;
    qint64 lastBytesRead = 0;

    while (totalRead < fileSize) {
        if (m_cancelled) break;

        // Pause Logic
        if (m_paused) {
            auto pauseStart = std::chrono::steady_clock::now();
            QMutexLocker locker(&m_sync);
            m_pauseCond.wait(&m_sync);

            // Add the time spent paused to our offset
            auto pauseEnd = std::chrono::steady_clock::now();
            m_totalPausedDuration += (pauseEnd - pauseStart);

            // Reset the sampling clock so the pause duration isn't 
            // counted as "active time" in the next speed calculation.
            lastSampleTime = pauseEnd;
            lastBytesRead = totalRead;
        }

        qint64 remaining = fileSize - totalRead;
        size_t toRead = std::min((qint64)Config::BUFFER_SIZE, remaining);

        // O_DIRECT handling: The count must be a multiple of 512/4096.
        // If we are at the end and 'toRead' is not a multiple, we must 
        // temporarily drop O_DIRECT or use a padded buffer.
        if (toRead % 4096 != 0) {
            // Simplest fix: Re-open without O_DIRECT for the last chunk 
            // OR just use posix_fadvise for the tail.
            fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) & ~O_DIRECT);
        }

        ssize_t n = read(fd, buffer.get(), toRead);
        if (n <= 0) break;

        XXH64_update(state, buffer.get(), n);
        totalRead += n;
        m_totalBytesProcessed += n;

        updateProgress(path, totalRead, fileSize, lastBytesRead, lastSampleTime);
    }

    // Force 100% and reset speed graph after verification
    emit progressChanged(QString::fromStdString(path.filename().string()), 
                        100, 0.0, 0.0, "");

    uint64_t diskHash = XXH64_digest(state);
    XXH64_freeState(state);
    close(fd);

    if (diskHash != expectedHash) {
        emit errorOccurred({QString::fromStdString(path.string()), "Checksum Mismatch!"});
        return false;
    }
    return true;
}
