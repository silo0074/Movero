#include <QDateTime>
#include <QRegularExpression>
#include <QStorageInfo>
#include <algorithm>
#include <cstdlib>
#include <fcntl.h>
#include <memory>
#include <unistd.h>
#include <xxhash.h>
#include <sys/stat.h>

#include "Config.h"
#include "CopyWorker.h"
#include "LogHelper.h"

namespace fs = std::filesystem;

// Constructor: Initializes the worker with source files, destination directory, and operation mode.
CopyWorker::CopyWorker(const std::vector<std::string> &sources, const std::string &destDir, Mode mode, QObject *parent)
	: QThread(parent),
	  m_sources(sources),
	  m_destDir(destDir),
	  m_mode(mode),
	  m_paused(false),
	  m_cancelled(false),
	  m_applyAll(false)
{}

// Pauses the copy operation.
void CopyWorker::pause() {
	m_paused = true;
}

// Resumes the copy operation if it was paused.
void CopyWorker::resume() {
	QMutexLocker locker(&m_sync);
	m_paused = false;
	m_pauseCond.wakeAll();
}

// Cancels the current operation and wakes up any waiting threads.
void CopyWorker::cancel() {
	m_cancelled = true;
	resume(); // Break wait if paused

	QMutexLocker locker(&m_inputMutex);
	m_inputWait.wakeAll(); // Break wait if waiting for user input
}

// Receives the user's decision on how to handle a file conflict.
void CopyWorker::resolveConflict(ConflictAction action, bool applyToAll, QString newName) {
	QMutexLocker locker(&m_inputMutex);
	m_userAction = action;
	m_applyAll = applyToAll;
	m_userNewName = newName;
	m_waitingForUser = false; // Signal that we are done
	m_inputWait.wakeAll();
}

// Generates a unique filename by appending a number (e.g., "file (1).txt") to avoid overwriting.
static fs::path generateAutoRename(const fs::path &path) {
	LOG(LogLevel::DEBUG) << "Input:" << path.string();
	fs::path folder = path.parent_path();
	QString stem = QString::fromStdString(path.stem().string());
	QString ext = QString::fromStdString(path.extension().string());

	LOG(LogLevel::DEBUG) << "AutoRename Input:" << stem << "Ext:" << ext;

	// Check if stem ends with (N)
	static const QRegularExpression re(R"(^(.*) \((\d{1,3})\)$)");
	QRegularExpressionMatch match = re.match(stem);

	int number = 1;
	QString baseName = stem;

	if (match.hasMatch()) {
		baseName = match.captured(1);
		number = match.captured(2).toInt() + 1;
		LOG(LogLevel::DEBUG) << "Regex Match! Base:" << baseName << "Next Number:" << number;
	} else {
		LOG(LogLevel::DEBUG) << "No Regex Match. Appending (1).";
	}

	while (true) {
		QString newName = QString("%1 (%2)%3").arg(baseName).arg(number).arg(ext);
		fs::path newPath = folder / newName.toStdString();
		if (!fs::exists(newPath)) {
			LOG(LogLevel::DEBUG) << "Generated unique name:" << QString::fromStdString(newPath.filename().string());
			return newPath;
		}
		number++;
	}
}

// Calculates current speed, average speed, and ETA, then emits progress signals to the UI.
void CopyWorker::updateProgress(const fs::path &src, const fs::path &dest, qint64 fileRead, qint64 fileSize)
{
	auto now = std::chrono::steady_clock::now();
	std::chrono::duration<double> elapsedSinceLast = now - m_lastSampleTime;

	if (elapsedSinceLast.count() < Config::SPEED_UPDATE_INTERVAL)
		return;

	// Current File Progress
	int filePercent = (int)((fileRead * 100) / fileSize);

	// Global Progress (Total bytes including Copy + Verify)
	// m_totalWorkBytes is (Total Size * 2)
	int totalPercent = (int)((m_totalBytesProcessed * 100) / m_totalWorkBytes);

	// Average Speed & ETA
	std::chrono::duration<double> totalActiveTime = (now - m_overallStartTime) - m_totalPausedDuration;
	double avgMbps = (m_totalBytesProcessed / (1024.0 * 1024.0)) / (totalActiveTime.count());
	uintmax_t bytesLeft = m_totalWorkBytes - m_totalBytesProcessed;
	long secondsLeft = -1;

	if (avgMbps > 0.01) {
		secondsLeft = static_cast<long>((bytesLeft / (1024.0 * 1024.0)) / avgMbps);
	}

	// Pass the instantaneous speed for the graph, and avg/eta for the labels
	double curMbps = ((m_totalBytesProcessed - m_lastTotalBytesProcessed) / (1024.0 * 1024.0)) / elapsedSinceLast.count();

	emit progressChanged(QString::fromStdString(src.string()),
		QString::fromStdString(dest.string()),
		filePercent, totalPercent, curMbps, avgMbps, secondsLeft);

	m_lastSampleTime = now;
	m_lastTotalBytesProcessed = m_totalBytesProcessed;
}

// Identifies the filesystem type (NTFS, FAT32, EXT, etc.) of the given path.
static CopyWorker::FileSystemType getFileSystemAt(const std::string &path)
{
	QStorageInfo storage(QString::fromStdString(path));
	// Important: QStorageInfo needs to be pointed at an existing directory/root
	// to identify the volume correctly.
	QByteArray type = storage.fileSystemType();

	if (type == "ntfs")
		return CopyWorker::FileSystemType::NTFS;
	if (type == "vfat" || type == "fat32" || type == "exfat")
		return CopyWorker::FileSystemType::FAT32;
	if (type.startsWith("ext") || type == "xfs")
		return CopyWorker::FileSystemType::EXT;

	return CopyWorker::FileSystemType::Generic;
}

// Removes or replaces characters that are invalid for the target filesystem.
static std::string sanitizeFilename(const std::string &name, CopyWorker::FileSystemType fsType)
{
	if (name.empty() || name == "." || name == "..")
		return name;

	// List of Windows Reserved Names
	static const std::vector<std::string> reserved = {
		"CON", "PRN", "AUX", "NUL", "COM1", "COM2", 
		"COM3", "COM4", "COM5", "COM6", "COM7", 
		"COM8", "COM9", "LPT1", "LPT2", "LPT3", 
		"LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"
	};

	bool isRestricted = (fsType == CopyWorker::FileSystemType::NTFS || fsType == CopyWorker::FileSystemType::FAT32);

	std::string result;
	if (isRestricted)
	{
		// Check for reserved names (case insensitive)
		std::string upperName = name;
		std::transform(upperName.begin(), upperName.end(), upperName.begin(), ::toupper);
		for (const auto &r : reserved) {
			if (upperName == r)	return "_" + name + "_";
		}

		for (char c : name)	{
			switch (c) {
				case '<':
					result += "\xE1\x90\xB8";
					break;
				case '>':
					result += "\xE1\x90\xB3";
					break;
				case ':':
					result += "\xEA\x9E\x89";
					break;
				case '"':
					result += "\xEF\xBC\x82";
					break;
				case '/':
					result += "\xE2\x88\x95";
					break;
				case '\\':
					result += "\xEF\xBC\xBC";
					break;
				case '|':
					result += "\xC7\x80";
					break;
				case '?':
					result += "\xEF\xBC\x9F";
					break;
				case '*':
					result += "\xEF\xBC\x8A";
					break;
				default:
					if (static_cast<unsigned char>(c) < 32)
						result += "_"; // Replace controls
					else
						result += c;
					break;
			}
		}
		// Handle trailing spaces/dots by replacing instead of popping
		if (!result.empty() && (result.back() == ' ' || result.back() == '.')) {
			result.back() = '_';
		}
	} else {
		// Linux/Unix: Only / and NUL are strictly forbidden
		for (char c : name)	{
			if (c == '/')
				result += "\xE2\x88\x95";
			else if (c != '\0')
				result += c;
		}
	}
	return result;
}

// Applies filename sanitization to an entire relative path structure.
static fs::path getSanitizedRelativePath(const fs::path &relPath, CopyWorker::FileSystemType fsType) {
	if (!Config::SANITIZE_FILENAMES)
		return relPath;

	fs::path sanitized;
	for (const auto &part : relPath) {
		sanitized /= sanitizeFilename(part.string(), fsType);
	}
	return sanitized;
}

// Main thread loop: Scans sources, checks disk space, creates directories, and iterates through file tasks.
void CopyWorker::run() {
	std::vector<CopyTask> tasks;
	std::vector<fs::path> sourceDirs; // To clean up empty folders in Move mode
	uintmax_t totalBytesRequired = 0;

	// Determine the destination filesystem type to apply correct sanitization rules.
	const CopyWorker::FileSystemType fsType = getFileSystemAt(m_destDir);

	if (Config::DRY_RUN) {
		// Simulate a file task
		totalBytesRequired = Config::DRY_RUN_FILE_SIZE;
		tasks.push_back({"DRY_RUN_SOURCE", fs::path(m_destDir) / "DRY_RUN.dat"});
		emit statusChanged(DryRunGenerating);

	} else {

		// PHASE 1: Scan, Map, and Calculate Size
		emit statusChanged(Scanning);


		for (const auto &srcStr : m_sources) {
			fs::path srcRoot(srcStr);
			if (!fs::exists(srcRoot)) {
				emit errorOccurred({ErrorType::SourceOpenFailed, QString::fromStdString(srcStr)});
				continue;
			}

			fs::path base = srcRoot.parent_path();

			if (fs::is_symlink(srcRoot)) {
				fs::path rel = fs::relative(srcRoot, base);
				tasks.push_back({srcRoot, fs::path(m_destDir) / getSanitizedRelativePath(rel, fsType), true});
			} else if (fs::is_directory(srcRoot)) {
				sourceDirs.push_back(srcRoot);
				fs::path rel = fs::relative(srcRoot, base);
				fs::path destDir = fs::path(m_destDir) / getSanitizedRelativePath(rel, fsType);
				tasks.push_back({srcRoot, destDir, true});

				for (const auto &entry : fs::recursive_directory_iterator(srcRoot)) {
					if (m_cancelled)
						return;
					if (entry.is_symlink()) {
						fs::path rel = fs::relative(entry.path(), base);
						tasks.push_back({entry.path(), fs::path(m_destDir) / getSanitizedRelativePath(rel, fsType), false});
					} else if (entry.is_directory()) {
						sourceDirs.push_back(entry.path());
						fs::path rel = fs::relative(entry.path(), base);
						fs::path destDir = fs::path(m_destDir) / getSanitizedRelativePath(rel, fsType);
						tasks.push_back({entry.path(), destDir, false});

					} else if (entry.is_regular_file()) {
						totalBytesRequired += entry.file_size();
						fs::path rel = fs::relative(entry.path(), base);
						tasks.push_back({entry.path(), fs::path(m_destDir) / getSanitizedRelativePath(rel, fsType), false});
					}
				}
			} else {
				totalBytesRequired += fs::file_size(srcRoot);
				fs::path rel = fs::relative(srcRoot, base);
				tasks.push_back({srcRoot, fs::path(m_destDir) / getSanitizedRelativePath(rel, fsType), true});
			}
		}
	}

	// PHASE 1.5: Verify Available Space
	uintmax_t safetyMargin = Config::DISK_SPACE_SAFETY_MARGIN;
	try {
		fs::space_info destSpace = fs::space(m_destDir);

		// Add a safety margin to account for filesystem overhead/metadata
		if (destSpace.available < (totalBytesRequired + safetyMargin)) {
			double reqGB = totalBytesRequired / (1024.0 * 1024.0 * 1024.0);
			double availGB = destSpace.available / (1024.0 * 1024.0 * 1024.0);

			emit errorOccurred({DiskFull,
				"",
				QString("%1|%2")
				.arg(reqGB, 0, 'f', 2)
				.arg(availGB, 0, 'f', 2)});
			return; // Terminate before starting
		}
	} catch (const fs::filesystem_error &e) {
		emit errorOccurred({DriveCheckFailed, "", ""});
		return;
	}

	// PHASE 2: Execute Tasks
	int totalFiles = tasks.size();
	int processed = 0;
	m_overallStartTime = std::chrono::steady_clock::now();
	m_totalPausedDuration = std::chrono::duration<double>::zero();
	m_totalBytesProcessed = 0;
	m_totalSizeToCopy = totalBytesRequired;
	m_totalWorkBytes = totalBytesRequired * (Config::CHECKSUM_ENABLED ? 2 : 1); // Copying + Optional Verifying
	// Prevent division by zero if the job consists only of empty folders (0 bytes)
	if (m_totalWorkBytes == 0) m_totalWorkBytes = 1;
	m_completedFilesSize = 0;
	m_lastSampleTime = m_overallStartTime;
	m_lastTotalBytesProcessed = 0;
	m_unflushedBytes = 0;
	m_totalBytesCopied = 0;

	// Adjust graph history size for small files to avoid empty looking graph
	// Heuristic: 1 MB per point. Min 50 points (5 seconds).
	int calculatedPoints = m_totalWorkBytes / (1024 * 1024) / 10;
	int minPoints = 10;
	Config::SPEED_GRAPH_HISTORY_SIZE = std::min(Config::SPEED_GRAPH_HISTORY_SIZE_USER, 
												std::max(minPoints, calculatedPoints));

	// Emit total files to copy
	emit totalProgress(processed, totalFiles);

	// Allocate buffer once for the entire job to avoid malloc/free overhead per file
	// Use aligned_alloc instead of std::vector for maximum performance
	size_t allocSize = Config::BUFFER_SIZE;
	if (allocSize % ALIGNMENT != 0){
		allocSize = (allocSize + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
	}
	
	// Allocate memory
	// aligned_alloc requires size to be a multiple of alignment
	void *rawPtr = std::aligned_alloc(ALIGNMENT, allocSize);
	if (!rawPtr) {
		emit errorOccurred({SourceOpenFailed, "", "Memory allocation failed"});
		return;
	}

	// Use unique_ptr with a custom deleter so it calls free() automatically
	std::unique_ptr<char, decltype(&std::free)> 
							buffer(static_cast<char *>(rawPtr), std::free);

	auto lastProgressTime = std::chrono::steady_clock::now();

	for (auto &task : tasks) {
		if (m_cancelled) break;
		fs::create_directories(task.dest.parent_path());

		bool isSymlink = fs::is_symlink(task.src);

		// Handle Directories
		if (fs::is_directory(task.src) && !isSymlink) {
			if (!fs::exists(task.dest)) {
				fs::create_directories(task.dest);
			}
			// Emit completion for top-level directories so they can be highlighted
			if (task.isTopLevel) {
				emit fileCompleted(QString::fromStdString(task.dest.string()), "", "", true);
			}
			if (Config::COPY_FILE_MODIFICATION_TIME) {
				std::error_code ec;
				fs::last_write_time(task.dest, fs::last_write_time(task.src, ec), ec);
			}
			processed++;
			// Throttle progress updates for directories
			auto now = std::chrono::steady_clock::now();
			if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastProgressTime).count() > 50) {
				emit totalProgress(processed, totalFiles);
				lastProgressTime = now;
			}
			continue;
		}

		// Space Check (Per File)
		uintmax_t currentFileSize = 0;
		if (!isSymlink) {
			try {
				currentFileSize = fs::file_size(task.src);
				// Check space (add safety margin)
				if (fs::space(m_destDir).available < (currentFileSize + safetyMargin)) {
					emit errorOccurred({DiskFull, QString::fromStdString(task.src.string())});
					break;
				}
			} catch (...) { /* Ignore space check errors, let write() fail if full */
			}
		}

		// Existence Check & Conflict Resolution
		if (fs::exists(task.dest) || fs::is_symlink(task.dest)) {
			ConflictAction action = m_savedAction;
			LOG(LogLevel::INFO) << "File already exists:" << task.dest.string();

			if (!m_applyAll) {
				fs::path suggested = generateAutoRename(task.dest);

				// Lock BEFORE emitting and setting the flag
				QMutexLocker locker(&m_inputMutex);
				m_waitingForUser = true;

				emit conflictNeeded(
					QString::fromStdString(task.src.string()),
					QString::fromStdString(task.dest.string()),
					QString::fromStdString(suggested.filename().string())
				);

				// Now wait safely
				while (m_waitingForUser && !m_cancelled) {
					m_inputWait.wait(&m_inputMutex);
				}
				action = m_userAction;

				if (m_applyAll)	m_savedAction = action;
			}

			if (action == Cancel) {
				m_cancelled = true;
				break;
			} else if (action == Skip) {
				processed++;
				// Adjust totals so progress bar jumps to correct %
				uintmax_t fSize = 0;
				try {
					fSize = fs::file_size(task.src);
				} catch (...) {
				}

				m_totalWorkBytes -= (fSize * (Config::CHECKSUM_ENABLED ? 2 : 1));
				m_totalSizeToCopy -= fSize;

				// Throttle progress
				auto now = std::chrono::steady_clock::now();
				if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastProgressTime).count() > 50) {
					emit totalProgress(processed, totalFiles);
					lastProgressTime = now;
				}
				continue;

			} else if (action == Rename) {
				if (!m_applyAll && !m_userNewName.isEmpty()) {
					task.dest = task.dest.parent_path() / m_userNewName.toStdString();
				} else {
					task.dest = generateAutoRename(task.dest);
				}
			}
			// If Replace, just proceed (O_TRUNC will handle it)
		}

		if (isSymlink) {
			try {
				// Check if destination is a directory
				if (fs::exists(task.dest) && fs::is_directory(task.dest) && !fs::is_symlink(task.dest))
				{
					// Safety: Don't remove a directory to place a symlink.
					// This prevents deleting mount points or folder structures.
					emit errorOccurred({DestinationIsDirectory, QString::fromStdString(task.dest.string())});
					processed++;
					emit totalProgress(processed, totalFiles);
					continue; // Skip this file and keep going
				}

				// Clear the path so the new link can be created.
				// The check fs::exists(task.dest) only returns true
				// if the destination points to a valid, existing target.
				// Using fs::is_symlink(task.dest), ensures that even broken links
				// are detected and removed before the copy operation proceeds.
				// Remove if it's an existing file or (potentially broken) symlink.
				if (fs::exists(task.dest) || fs::is_symlink(task.dest)) {
					fs::remove(task.dest);
				}
				fs::copy_symlink(task.src, task.dest);

				if (Config::COPY_FILE_MODIFICATION_TIME) {
					struct stat st;
					if (lstat(task.src.c_str(), &st) == 0) {
						struct timespec times[2];
						times[0] = st.st_atim; // Access time
						times[1] = st.st_mtim; // Modification time
						if (utimensat(AT_FDCWD, task.dest.c_str(), times, AT_SYMLINK_NOFOLLOW) != 0) {
							LOG(LogLevel::WARNING) << "Failed to set symlink timestamp: " << task.dest.string();
						}
					}
				}

				if (m_mode == Move && !Config::DRY_RUN)	{
					fs::remove(task.src);
				}

				// Emit completion for symlinks
				if (task.isTopLevel) {
					emit fileCompleted(QString::fromStdString(task.dest.string()), "", "", true);
				}

				// Update UI so it doesn't look stuck
				emit progressChanged(QString::fromStdString(task.src.string()), 
									QString::fromStdString(task.dest.string()),
									100, // File is "100%" done
									(int)((m_totalBytesProcessed * 100) / m_totalWorkBytes),
									0.0,
									0.0,
									0
				);
			} catch (...) {
				emit errorOccurred({WriteError, QString::fromStdString(task.src.string())});
			}
			processed++;
			// Throttle progress
			auto now = std::chrono::steady_clock::now();
			if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastProgressTime).count() > 50) {
				emit totalProgress(processed, totalFiles);
				lastProgressTime = now;
			}
			continue;
		}

		// copyFile returns true ONLY if checksum verification succeeds
		if (copyFile(task.src, task.dest, buffer.get(), allocSize, task.isTopLevel, 
					(&task == &tasks.back()), fsType)) 
		{
			if (m_mode == Move && !Config::DRY_RUN) {
				fs::remove(task.src);
			}
			m_completedFilesSize += currentFileSize;
		}
		processed++;
		
		// Throttle total progress updates (e.g. max 20 times per second)
		auto now = std::chrono::steady_clock::now();
		if (processed == totalFiles || std::chrono::duration_cast<std::chrono::milliseconds>(now - lastProgressTime).count() > 50) {
			emit totalProgress(processed, totalFiles);
				lastProgressTime = now;
		}
	}

	// PHASE 3: Cleanup (Move Mode Only)
	// We only reach this if we are moving folders
	if (m_mode == Move && !m_cancelled) {
		emit statusChanged(RemovingEmptyFolders);

		// Sort by length descending: ensures /A/B/C is deleted before /A/B/
		std::sort(sourceDirs.begin(), sourceDirs.end(), [](const fs::path &a, const fs::path &b) {
			return a.string().length() > b.string().length();
		});

		for (const auto &dir : sourceDirs) {
			try {
				if (fs::exists(dir) && fs::is_directory(dir) && fs::is_empty(dir)) {
					fs::remove(dir);
				}
			} catch (...) { /* Skip folders that still have files due to errors */
			}
		}
	}

	emit finished();
}


// Handles the low-level copying of a single file: reading, writing, calculating hash, and syncing to disk.
bool CopyWorker::copyFile(const fs::path &src, const fs::path &dest, char *buffer, size_t bufferSize, bool isTopLevel, bool isLastFile, FileSystemType fsType) {
	int fd_in = -1;

	// Open the source file only if not in dry run mode
	if (!Config::DRY_RUN) {
		// Low-level I/O for speed and control
		fd_in = open(src.c_str(), O_RDONLY);

		if (fd_in < 0) {
			emit errorOccurred({SourceOpenFailed, QString::fromStdString(src.string())});
			return false;
		}

		// Hint kernel: We will read this once, sequentially. Don't cache aggressively.
		posix_fadvise(fd_in, 0, 0, POSIX_FADV_SEQUENTIAL | POSIX_FADV_NOREUSE);
	}

	// Open O_RDWR so we can read it back for verification without closing/reopening
	int fd_out = open(dest.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);

	if ((!Config::DRY_RUN && fd_in < 0) || (fd_out < 0)) {
		emit errorOccurred({FileOpenFailed, QString::fromStdString(src.string())});
		if (fd_in >= 0)
			close(fd_in);
		if (fd_out >= 0)
			close(fd_out);
		return false;
	}

	XXH64_state_t *hashState = nullptr;
	if (Config::CHECKSUM_ENABLED) {
		hashState = XXH64_createState();
		XXH64_reset(hashState, 0);
	}

	qint64 totalRead = 0;
	qint64 fileSize = Config::DRY_RUN ? (Config::DRY_RUN_FILE_SIZE) : fs::file_size(src);

	emit statusChanged(Copying); // Notify UI

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
			m_lastSampleTime = pauseEnd;
			m_lastTotalBytesProcessed = m_totalBytesProcessed;
		}

		size_t toRead = std::min((qint64)bufferSize, fileSize - totalRead);
		ssize_t bytesRead;

		if (Config::DRY_RUN) {
			bytesRead = toRead; // Simulate reading from disk
			// Simulate disk latency (roughly 10ms for a mechanical seek/read)
			// This prevents the "instant" processing that causes GB/s spikes
			QThread::msleep(10);
		} else {
			bytesRead = read(fd_in, buffer, toRead);
		}

		if (bytesRead < 0) {
			emit errorOccurred({ReadError, QString::fromStdString(src.string())});
			break;
		}
		if (bytesRead == 0) {
			emit errorOccurred({UnexpectedEOF, QString::fromStdString(src.string())});
			break;
		}

		// Calculate Hash on the fly
		if (Config::CHECKSUM_ENABLED) {
			XXH64_update(hashState, buffer, bytesRead);
		}

		// Write
		ssize_t written = write(fd_out, buffer, bytesRead);
		if (written != bytesRead) {
			emit errorOccurred({WriteError, QString::fromStdString(src.string())});
			break;
		}

		totalRead += bytesRead;
		m_totalBytesProcessed += bytesRead;
		m_unflushedBytes += bytesRead;
		m_totalBytesCopied += bytesRead;

		// Calculate and update speed
		updateProgress(src, dest, totalRead, fileSize);
	}

	// --- CLEANUP & CHECK PHASE ---

	// If cancelled or incomplete, clean up and return
	if (m_cancelled || totalRead != fileSize) {
		if (Config::CHECKSUM_ENABLED)
			XXH64_freeState(hashState);
		if (fd_in >= 0)
			close(fd_in);
		if (fd_out >= 0)
			close(fd_out);

		// Delete the partial file
		LOG(LogLevel::INFO) << "Removing partial file:" << dest.string();
		LOG(LogLevel::INFO) << "Reason: cancelled =" << m_cancelled
								<< ", fileSize =" << fileSize << ", totalRead =" << totalRead;
		try {
			fs::remove(dest);
		} catch (...) {
		}
		m_totalBytesCopied -= totalRead;
		return false;
	}

	// Force 100% and reset speed graph after copying
	int totalPercent = (int)((m_totalBytesProcessed * 100) / m_totalWorkBytes);

	emit progressChanged(
		QString::fromStdString(src.string()),
		QString::fromStdString(dest.string()),
		100, totalPercent, 0, 0, 0
	);

	// Grouped Syncing Logic
	// Only sync if we have accumulated enough bytes or this is the last file.
	bool shouldSync = (m_unflushedBytes >= 64 * 1024 * 1024) || isLastFile;
	bool useSyncFileRange = (fsType == FileSystemType::EXT); // Only for EXT and XFS

	// Start flushing to disk asynchronously while we calculate the hash
	if (shouldSync && useSyncFileRange) {
		// Start the write-out (Non-blocking)
		sync_file_range(fd_out, 0, 0, SYNC_FILE_RANGE_WRITE);
		LOG(LogLevel::DEBUG) << "---------------------------sync_file_range";
	}

	// If we are here, the copy phase finished successfully.
	// Calculate Source Hash
	uint64_t srcHash = 0;
	if (Config::CHECKSUM_ENABLED) {
		LOG(LogLevel::DEBUG) << "Generating Source Hash...";
		emit statusChanged(GeneratingHash);
		srcHash = XXH64_digest(hashState);
		XXH64_freeState(hashState);
	}

	if (fd_in >= 0)	close(fd_in);

	// Ensure data is on disk before verification
	// We only force sync if Checksum is enabled (to verify from disk) OR if Moving (safety).
	if (shouldSync && (Config::CHECKSUM_ENABLED || m_mode == Move)) {
		if (useSyncFileRange) {
			// Wait for completion (Blocking)
			sync_file_range(fd_out, 0, 0, SYNC_FILE_RANGE_WAIT_BEFORE | SYNC_FILE_RANGE_WRITE | SYNC_FILE_RANGE_WAIT_AFTER);
		} else {
			// Fallback for other filesystems
			fdatasync(fd_out);
		}
		
		// Reset counter after sync
		m_unflushedBytes = 0;
	}

	// File modification time
	// Using std::error_code is a good safety measure to prevent exceptions
	// if a specific file's metadata cannot be accessed.
	// Note: birth_time (creation) is not part of the C++ standard
	// filesystem library yet because of OS-specific limitations.
	if (Config::COPY_FILE_MODIFICATION_TIME) {
		std::error_code ec;
		fs::last_write_time(dest, fs::last_write_time(src, ec), ec);
	}

	// Only drop cache if we actually synced (meaning we hit the threshold)
	if (Config::CHECKSUM_ENABLED && shouldSync) {
		// Tell the OS: "I'm done with this, throw it out of RAM."
		posix_fadvise(fd_out, 0, 0, POSIX_FADV_DONTNEED);
	}

	// Verify Phase (Read from disk)
	uint64_t diskHash = 0;
	bool checksumFailed = false;

	if (Config::CHECKSUM_ENABLED) {
		if (!verifyFile(src, dest, fd_out, srcHash, diskHash, buffer, bufferSize)) {
			LOG(LogLevel::ERROR) << "Verification failed:" << dest.c_str();
			// Verification failed or was cancelled during verification
			try {
				LOG(LogLevel::INFO) << "Removing failed checksum destination file:" << dest.c_str();
				fs::remove(dest);
			} catch (...) {	}
			m_totalBytesCopied -= totalRead;
			checksumFailed = true;
		}
	}

	close(fd_out);

	// Emit completion signal with hashes
	emit fileCompleted(
		QString::fromStdString(dest.string()),
		Config::CHECKSUM_ENABLED ? QString::number(srcHash, 16) : "",
		Config::CHECKSUM_ENABLED ? QString::number(diskHash, 16) : "",
		isTopLevel
	);

	if (checksumFailed){
		emit errorOccurred({ChecksumMismatch, QString::fromStdString(dest.string())});
	}

	return true;
}


// Verifies the integrity of the copied file by reading it back from disk and comparing checksums.
bool CopyWorker::verifyFile(
	const std::filesystem::path &src,
	const std::filesystem::path &dest,
	int fd_dest,
	uint64_t expectedHash,
	uint64_t &outDiskHash,
	char *buffer,
	size_t bufferSize) 
{
	uintmax_t syncThreshold = static_cast<uintmax_t>(Config::SYNC_THRESHOLD_MB) * 1024 * 1024;
	emit statusChanged(Verifying); // Update UI status

	// Hybrid Strategy for Verification:
	// Small files: Standard buffered read (fast, reads from Cache if we skipped fdatasync).
	// Large files: O_DIRECT (safe, reads from Disk, requires fdatasync to have happened).
	bool useDirect = false;
	qint64 fileSize = fs::file_size(dest);

	// Optimization: Skip O_DIRECT entirely for files smaller than alignment
	if (fileSize >= syncThreshold && fileSize >= ALIGNMENT) {
		useDirect = true;
	}

	// Reuse the existing file descriptor
	int fd = fd_dest;
	lseek(fd, 0, SEEK_SET);

	// Try to enable O_DIRECT on the existing FD if requested
	int originalFlags = fcntl(fd, F_GETFL);
	if (useDirect) {
		if (fcntl(fd, F_SETFL, originalFlags | O_DIRECT) < 0) {
			useDirect = false; // Fallback to buffered if O_DIRECT fails
		}
	}
	
	if (!useDirect) {
		// Hint sequential access for buffered reads
		// If O_DIRECT failed, we try to clear the cache so we read from disk
		// POSIX_FADV_DONTNEED in the fallback path: This attempts to clear the cache
		// before reading, maximizing the chance of a physical disk read even without O_DIRECT.
		posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
		posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL);
	}

	XXH64_state_t *state = XXH64_createState();
	XXH64_reset(state, 0);

	qint64 totalRead = 0;

	while (totalRead < fileSize) {
		if (m_cancelled)
			break;

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
			m_lastSampleTime = pauseEnd;
			m_lastTotalBytesProcessed = m_totalBytesProcessed;
		}

		qint64 remaining = fileSize - totalRead;
		size_t toRead = std::min((qint64)bufferSize, remaining);

		// O_DIRECT handling: Read size must be aligned.
		// If we are at the tail and it's not aligned, drop O_DIRECT.
		if (useDirect && (toRead % ALIGNMENT != 0)) {
			int currentFlags = fcntl(fd, F_GETFL);
			fcntl(fd, F_SETFL, currentFlags & ~O_DIRECT);
			useDirect = false; // Stays off for the remainder (which is just this last chunk)
		}

		ssize_t n = read(fd, buffer, toRead);
		if (n <= 0)	break;

		XXH64_update(state, buffer, n);
		totalRead += n;
		m_totalBytesProcessed += n;
		updateProgress(src, dest, totalRead, fileSize);
	}

	// Force 100% and reset speed graph after verification
	int totalPercent = (int)((m_totalBytesProcessed * 100) / m_totalWorkBytes);

	emit progressChanged(
		QString::fromStdString(src.string()),
		QString::fromStdString(dest.string()),
		100, totalPercent, 0, 0, 0
	);

	outDiskHash = XXH64_digest(state);
	XXH64_freeState(state);

	// Restore flags if we changed them (though we are about to close it in copyFile)
	if (useDirect) {
		fcntl(fd, F_SETFL, originalFlags);
	}

	if (outDiskHash != expectedHash) {
		return false;
	}
	return true;
}
