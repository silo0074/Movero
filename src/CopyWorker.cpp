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

CopyWorker::CopyWorker(const std::vector<std::string> &sources, const std::string &destDir, Mode mode, QObject *parent)
	: QThread(parent),
	  m_sources(sources),
	  m_destDir(destDir),
	  m_mode(mode),
	  m_paused(false),
	  m_cancelled(false),
	  m_applyAll(false)
{}

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

	QMutexLocker locker(&m_inputMutex);
	m_inputWait.wakeAll(); // Break wait if waiting for user input
}

void CopyWorker::resolveConflict(ConflictAction action, bool applyToAll, QString newName) {
	QMutexLocker locker(&m_inputMutex);
	m_userAction = action;
	m_applyAll = applyToAll;
	m_userNewName = newName;
	m_waitingForUser = false; // Signal that we are done
	m_inputWait.wakeAll();
}

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

void CopyWorker::updateProgress(const fs::path &src, const fs::path &dest, qint64 fileRead, qint64 fileSize,
	qint64 &lastBytesRead, std::chrono::steady_clock::time_point &lastSampleTime) {

	auto now = std::chrono::steady_clock::now();
	std::chrono::duration<double> elapsedSinceLast = now - lastSampleTime;

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

	if (avgMbps > 0.5) {
		secondsLeft = static_cast<long>((bytesLeft / (1024.0 * 1024.0)) / avgMbps);
	}

	// Pass the instantaneous speed for the graph, and avg/eta for the labels
	double curMbps = ((fileRead - lastBytesRead) / (1024.0 * 1024.0)) / elapsedSinceLast.count();

	emit progressChanged(QString::fromStdString(src.string()),
		QString::fromStdString(dest.string()),
		filePercent, totalPercent, curMbps, avgMbps, secondsLeft);

	lastSampleTime = now;
	lastBytesRead = fileRead;
}

enum class FileSystemType {
	NTFS,
	FAT32,
	EXT, // ext2/3/4
	Generic
};

static FileSystemType getFileSystemAt(const std::string &path)
{
	QStorageInfo storage(QString::fromStdString(path));
	// Important: QStorageInfo needs to be pointed at an existing directory/root
	// to identify the volume correctly.
	QString type = storage.fileSystemType();

	if (type == "ntfs")
		return FileSystemType::NTFS;
	if (type == "vfat" || type == "fat32" || type == "exfat")
		return FileSystemType::FAT32;
	if (type.startsWith("ext"))
		return FileSystemType::EXT;

	return FileSystemType::Generic;
}

static std::string sanitizeFilename(const std::string &name, FileSystemType fsType)
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

	bool isRestricted = (fsType == FileSystemType::NTFS || fsType == FileSystemType::FAT32);

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

static fs::path getSanitizedRelativePath(const fs::path &relPath, FileSystemType fsType) {
	if (!Config::SANITIZE_FILENAMES)
		return relPath;

	fs::path sanitized;
	for (const auto &part : relPath) {
		sanitized /= sanitizeFilename(part.string(), fsType);
	}
	return sanitized;
}

void CopyWorker::run() {
	std::vector<CopyTask> tasks;
	std::vector<fs::path> sourceDirs; // To clean up empty folders in Move mode
	uintmax_t totalBytesRequired = 0;

	if (Config::DRY_RUN) {
		// Simulate a file task
		totalBytesRequired = Config::DRY_RUN_FILE_SIZE;
		tasks.push_back({"DRY_RUN_SOURCE", fs::path(m_destDir) / "DRY_RUN.dat"});
		emit statusChanged(DryRunGenerating);

	} else {

		// PHASE 1: Scan, Map, and Calculate Size
		emit statusChanged(Scanning);

		// Determine the destination filesystem type to apply correct sanitization rules.
		const FileSystemType fsType = getFileSystemAt(m_destDir);

		for (const auto &srcStr : m_sources) {
			fs::path srcRoot(srcStr);
			if (!fs::exists(srcRoot)) {
				emit errorOccurred({ErrorType::SourceOpenFailed, QString::fromStdString(srcStr)});
				continue;
			}

			fs::path base = srcRoot.parent_path();

			if (fs::is_symlink(srcRoot)) {
				fs::path rel = fs::relative(srcRoot, base);
				tasks.push_back({srcRoot, fs::path(m_destDir) / getSanitizedRelativePath(rel, fsType)});
			} else if (fs::is_directory(srcRoot)) {
				sourceDirs.push_back(srcRoot);
				for (const auto &entry : fs::recursive_directory_iterator(srcRoot)) {
					if (m_cancelled)
						return;
					if (entry.is_symlink()) {
						fs::path rel = fs::relative(entry.path(), base);
						tasks.push_back({entry.path(), fs::path(m_destDir) / getSanitizedRelativePath(rel, fsType)});
					} else if (entry.is_directory()) {
						sourceDirs.push_back(entry.path());
					} else if (entry.is_regular_file()) {
						totalBytesRequired += entry.file_size();
						fs::path rel = fs::relative(entry.path(), base);
						tasks.push_back({entry.path(), fs::path(m_destDir) / getSanitizedRelativePath(rel, fsType)});
					}
				}
			} else {
				totalBytesRequired += fs::file_size(srcRoot);
				fs::path rel = fs::relative(srcRoot, base);
				tasks.push_back({srcRoot, fs::path(m_destDir) / getSanitizedRelativePath(rel, fsType)});
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
	m_completedFilesSize = 0;

	// Adjust graph history size for small files to avoid empty looking graph
	// Heuristic: 1 MB per point. Min 50 points (5 seconds).
	int calculatedPoints = m_totalWorkBytes / (1024 * 1024) / 10;
	int minPoints = 10;
	Config::SPEED_GRAPH_HISTORY_SIZE = std::min(Config::SPEED_GRAPH_HISTORY_SIZE_USER, 
												std::max(minPoints, calculatedPoints));
	m_totalBytesCopied = 0;

	// Emit total files to copy
	emit totalProgress(processed, totalFiles);

	for (auto &task : tasks) {
		if (m_cancelled)
			break;
		fs::create_directories(task.dest.parent_path());

		bool isSymlink = fs::is_symlink(task.src);
		// 1. Space Check (Per File)
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

		// 2. Existence Check & Conflict Resolution
		if (fs::exists(task.dest) || fs::is_symlink(task.dest)) {
			ConflictAction action = m_savedAction;
			LOG(LogLevel::DEBUG) << "File already exists:" << task.dest.string();

			if (!m_applyAll) {
				fs::path suggested = generateAutoRename(task.dest);

				// Lock BEFORE emitting and setting the flag
				QMutexLocker locker(&m_inputMutex);
				m_waitingForUser = true;

				emit conflictNeeded(QString::fromStdString(task.src.string()),
					QString::fromStdString(task.dest.string()),
					QString::fromStdString(suggested.filename().string()));

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

				emit totalProgress(processed, totalFiles);
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
			emit totalProgress(processed, totalFiles);
			continue;
		}

		// copyFile returns true ONLY if verification succeeds
		LOG(LogLevel::DEBUG) << "Copying file";
		if (copyFile(task.src, task.dest)) { // copyFile verifies checksum
			if (m_mode == Move && !Config::DRY_RUN) {
				fs::remove(task.src);
			}
			m_completedFilesSize += currentFileSize;
		}
		processed++;
		emit totalProgress(processed, totalFiles);
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

bool CopyWorker::copyFile(const fs::path &src, const fs::path &dest) {
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

	int fd_out = open(dest.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);

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

	// Use aligned_alloc instead of std::vector for maximum performance
	// See verifyFile() for comments.
	const size_t ALIGNMENT = 4096; // Standard page size

	// Allocate memory
	// aligned_alloc requires size to be a multiple of alignment
	size_t allocSize = Config::BUFFER_SIZE;
	if (allocSize % ALIGNMENT != 0)
		allocSize = (allocSize + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
	void *rawPtr = std::aligned_alloc(ALIGNMENT, allocSize);
	if (!rawPtr)
		return false;

	// Use unique_ptr with a custom deleter so it calls free() automatically
	std::unique_ptr<char, decltype(&std::free)> buffer(static_cast<char *>(rawPtr), std::free);

	// std::vector<char> buffer(BUFFER_SIZE, 0); // Initialize with zeros

	qint64 totalRead = 0;
	qint64 fileSize = Config::DRY_RUN ? (Config::DRY_RUN_FILE_SIZE) : fs::file_size(src);
	auto startTime = std::chrono::steady_clock::now();
	auto lastSampleTime = startTime;
	ssize_t bytesRead;
	qint64 lastBytesRead = 0;

	emit statusChanged(Copying); // Notify UI

	// Read source file and write to destination
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
			lastSampleTime = pauseEnd;
			lastBytesRead = totalRead;
		}

		size_t toRead = std::min((qint64)Config::BUFFER_SIZE, fileSize - totalRead);
		ssize_t bytesRead;

		if (Config::DRY_RUN) {
			bytesRead = toRead; // Simulate reading from disk
			// Simulate disk latency (roughly 10ms for a mechanical seek/read)
			// This prevents the "instant" processing that causes GB/s spikes
			QThread::msleep(10);
		} else {
			bytesRead = read(fd_in, buffer.get(), toRead);
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
			XXH64_update(hashState, buffer.get(), bytesRead);
		}

		// Write
		ssize_t written = write(fd_out, buffer.get(), bytesRead);
		if (written != bytesRead) {
			emit errorOccurred({WriteError, QString::fromStdString(src.string())});
			break;
		}

		totalRead += bytesRead;
		m_totalBytesProcessed += bytesRead;
		m_totalBytesCopied += bytesRead;

		// Calculate and update speed
		updateProgress(src, dest, totalRead, fileSize, lastBytesRead, lastSampleTime);
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

	// If we are here, the copy phase finished successfully.
	// Calculate Source Hash
	uint64_t srcHash = 0;
	if (Config::CHECKSUM_ENABLED) {
		LOG(LogLevel::DEBUG) << "Generating Source Hash...";
		emit statusChanged(GeneratingHash);
		srcHash = XXH64_digest(hashState);
		XXH64_freeState(hashState);
	}

	if (fd_in >= 0)
		close(fd_in);

	// Ensure data is on disk before verification
	fdatasync(fd_out);
	close(fd_out);

	// File modification time
	// Using std::error_code is a good safety measure to prevent exceptions
	// if a specific file's metadata cannot be accessed.
	// Note: birth_time (creation) is not part of the C++ standard
	// filesystem library yet because of OS-specific limitations.
	if (Config::COPY_FILE_MODIFICATION_TIME) {
		std::error_code ec;
		fs::last_write_time(dest, fs::last_write_time(src, ec), ec);
	}

	// Force 100% and reset speed graph after copying
	int totalPercent = (int)((m_totalBytesProcessed * 100) / m_totalWorkBytes);
	emit progressChanged(QString::fromStdString(src.string()),
		QString::fromStdString(dest.string()),
		100, totalPercent, 0.0, 0.0, 0);

	// Open the file again briefly just to invalidate the cache for it
	int fd_drop = open(dest.c_str(), O_RDONLY);
	if (fd_drop >= 0) {
		// Tell the OS: "I'm done with this, throw it out of RAM."
		posix_fadvise(fd_drop, 0, 0, POSIX_FADV_DONTNEED);
		close(fd_drop);
	}

	// Verify Phase (Read from disk)
	uint64_t diskHash = 0;
	if (Config::CHECKSUM_ENABLED) {
		if (!verifyFile(src, dest, srcHash, diskHash)) {
			LOG(LogLevel::DEBUG) << "Verification failed=" << dest.string();
			// Verification failed or was cancelled during verification
			try {
				fs::remove(dest);
			} catch (...) {
			}
			m_totalBytesCopied -= totalRead;
			return false;
		}
	}

	// Emit completion signal with hashes
	emit fileCompleted(QString::fromStdString(src.string()),
		Config::CHECKSUM_ENABLED ? QString::number(srcHash, 16) : "",
		Config::CHECKSUM_ENABLED ? QString::number(diskHash, 16) : "");

	return true;
}


bool CopyWorker::verifyFile(const std::filesystem::path &src,
	const std::filesystem::path &dest,
	uint64_t expectedHash,
	uint64_t &outDiskHash) {
	emit statusChanged(Verifying); // Update UI status

	// Open with O_DIRECT to bypass OS cache entirely
	// O_DIRECT: This forces the read to bypass the OS Page Cache,
	// ensuring the checksum is calculated against the actual bits on the physical media.
	int fd = open(dest.c_str(), O_RDONLY | O_DIRECT);

	// Fallback: Some filesystems/OSs don't support O_DIRECT
	// For simplicity and compatibility, we use posix_fadvise(DONTNEED) to urge reading from disk.
	if (fd < 0) {
		fd = open(dest.c_str(), O_RDONLY);
		if (fd >= 0) {
			// If O_DIRECT failed, we try to clear the cache so we read from disk
			// POSIX_FADV_DONTNEED in the fallback path: This attempts to clear the cache
			// before reading, maximizing the chance of a physical disk read even without O_DIRECT.
			posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
			posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL);
		}
	}

	if (fd < 0)
		return false;

	XXH64_state_t *state = XXH64_createState();
	XXH64_reset(state, 0);

	// std::aligned_alloc vs std::vector: std::aligned_alloc is superior here for two reasons:
	// Alignment: O_DIRECT (Direct I/O) strictly requires memory buffers to be aligned to
	// the block size (usually 4096 bytes). std::vector does not guarantee this alignment.
	// Initialization: std::vector zero-initializes memory upon creation.
	// For an 8MB buffer, this is wasted CPU cycles. aligned_alloc gives you raw memory,
	// which is faster since you are about to overwrite it anyway.

	// Use aligned_alloc instead of std::vector for maximum performance
	const size_t ALIGNMENT = 4096; // Standard page size

	// Allocate memory
	size_t allocSize = Config::BUFFER_SIZE;
	if (allocSize % ALIGNMENT != 0)
		allocSize = (allocSize + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
	void *rawPtr = std::aligned_alloc(ALIGNMENT, allocSize);

	if (!rawPtr)
		return false;

	// Use unique_ptr with a custom deleter so it calls free() automatically
	std::unique_ptr<char, decltype(&std::free)> buffer(static_cast<char *>(rawPtr), std::free);

	// std::vector<char> buffer(BUFFER_SIZE);
	// ssize_t n;

	// Start the timer for verification speed
	auto startTime = std::chrono::steady_clock::now();
	auto lastSampleTime = startTime;
	qint64 lastBytesRead = 0;
	qint64 fileSize = fs::file_size(dest);
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
		if (n <= 0)
			break;

		XXH64_update(state, buffer.get(), n);
		totalRead += n;
		m_totalBytesProcessed += n;

		updateProgress(src, dest, totalRead, fileSize, lastBytesRead, lastSampleTime);
	}

	// Force 100% and reset speed graph after verification
	int totalPercent = (int)((m_totalBytesProcessed * 100) / m_totalWorkBytes);
	emit progressChanged(QString::fromStdString(src.string()),
		QString::fromStdString(dest.string()),
		100, totalPercent, 0.0, 0.0, 0);

	outDiskHash = XXH64_digest(state);
	XXH64_freeState(state);
	close(fd);

	if (outDiskHash != expectedHash) {
		emit errorOccurred({ChecksumMismatch, QString::fromStdString(dest.string())});
		return false;
	}
	return true;
}
