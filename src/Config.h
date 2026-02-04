#pragma once

#include <QString>
#include <cstdint>

namespace Constants {
	inline constexpr char DESCRIPTION[] = QT_TRANSLATE_NOOP("Settings",
		"A customizable file copier with features "
		"such as file integrity check "
		"and a speed vs time graph.");

	inline constexpr char GITHUB_URL[] = "https://github.com/silo0074/Movero";
	inline constexpr char WEBSITE_URL[] = "https://www.programming-electronics-diy.xyz/";
	inline constexpr char DONATE_URL[] = "https://www.buymeacoffee.com/liviuistrate";
	inline constexpr char DONATE_IMG[] = ":/images/buy_me_a_coffee-default-yellow.png";
} // namespace Constants

namespace Config {
	namespace Defaults {
		inline constexpr bool LOG_HISTORY_ENABLED = true;
		inline constexpr bool CLOSE_ON_FINISH = false;
		inline constexpr bool CHECKSUM_ENABLED = true;
		inline constexpr bool COPY_FILE_MODIFICATION_TIME = true;
		inline constexpr bool SANITIZE_FILENAMES = true;
		inline constexpr char UI_STYLE[] = "";
		inline constexpr char LANGUAGE[] = "en";

		inline constexpr bool SPEED_GRAPH_SHOW_TIME_LABELS = true;
		inline constexpr bool SPEED_GRAPH_ALIGN_LABELS_RIGHT = false;
		inline constexpr int SPEED_GRAPH_HISTORY_SIZE = 200;
		inline constexpr int SPEED_GRAPH_HISTORY_SIZE_USER = 200;
		inline constexpr double SPEED_GRAPH_MAX_SPEED = 10.0;
		inline constexpr int SYNC_THRESHOLD_MB = 4;
		inline constexpr bool SELECT_FILES_AFTER_COPY = true;
	} // namespace Defaults

	// ----------- App details ------------------
	// Defined by CMakeLists
	// inline constexpr char APP_NAME[] = "Movero";
	// inline constexpr char APP_VERSION[] = "1.0.0";
	inline constexpr char DEVELOPER[] = "Liviu Istrate";

	// ----------- App settings ------------------
	// Saves transferred files and potential errors to a file
	// and displays them in the more details window.
	inline bool LOG_HISTORY_ENABLED = Defaults::LOG_HISTORY_ENABLED;

	// UI data update interval. Must be at least twice the SPEED_UPDATE_INTERVAL.
	inline constexpr int UPDATE_INTERVAL_MS = 100;

	// Set to true to bypass clipboard and test with a dummy file
	inline constexpr bool DRY_RUN = false;
	inline constexpr uintmax_t DRY_RUN_FILE_SIZE = 4ULL * 1024 * 1024 * 1024;

	// Auto-close window when finished
	inline bool CLOSE_ON_FINISH = Defaults::CLOSE_ON_FINISH;

	// Verify file integrity (checksum) after copy
	inline bool CHECKSUM_ENABLED = Defaults::CHECKSUM_ENABLED;

	// File modification time
	inline bool COPY_FILE_MODIFICATION_TIME = Defaults::COPY_FILE_MODIFICATION_TIME;

	// Sanitize filenames
	inline bool SANITIZE_FILENAMES = Defaults::SANITIZE_FILENAMES;

	// Style
	inline QString UI_STYLE = Defaults::UI_STYLE;

	// Language
	inline QString LANGUAGE = Defaults::LANGUAGE;


	// ----------- UI constants -----------------
	inline constexpr int WINDOW_WIDTH = 650;
	inline constexpr int WINDOW_HEIGHT_EXPANDED = 700;
	inline constexpr int SPEED_GRAPH_MIN_HEIGHT = 200;

	// ----------- Speed Graph ------------------
	// Speed Graph Visuals
	inline bool SPEED_GRAPH_SHOW_TIME_LABELS = Defaults::SPEED_GRAPH_SHOW_TIME_LABELS;
	inline bool SPEED_GRAPH_ALIGN_LABELS_RIGHT = Defaults::SPEED_GRAPH_ALIGN_LABELS_RIGHT; // false = left, true = right

	// Colors (AARRGGBB format for QColor)
	inline constexpr unsigned int COLOR_GRAPH_ACTIVE = 0xFF00B400; // Green
	inline constexpr unsigned int COLOR_GRAPH_PAUSED = 0xFFFF8C00; // Orange
	inline constexpr unsigned int COLOR_GRAPH_GRADIENT_ACTIVE = 0x6400FF00; // Green with alpha
	inline constexpr unsigned int COLOR_GRAPH_GRADIENT_PAUSED = 0x64FFA500; // Orange with alpha
	inline constexpr unsigned int COLOR_GRAPH_GRID = 0x64C8C8C8; // Light Gray with alpha
	inline constexpr unsigned int COLOR_GRAPH_TEXT = 0xFF808080; // Gray

	// 200 points will represent 20 seconds of history at 10Hz (200 * 0.1s)
	// History = SPEED_GRAPH_HISTORY_SIZE * (UPDATE_INTERVAL_MS / 1000.0)
	inline int SPEED_GRAPH_HISTORY_SIZE = Defaults::SPEED_GRAPH_HISTORY_SIZE;
	inline int SPEED_GRAPH_HISTORY_SIZE_USER = Defaults::SPEED_GRAPH_HISTORY_SIZE_USER;

	// m_maxSpeed represents the top value of the Y-Axis (the 100% height of the graph).
	// This is a "floor" or minimum scale. If you are copying a very small file at 2 MB/s,
	// you don't want the graph to scale its max height to exactly 2 MB/s
	// (which makes the graph look like it's "maxing out"). By setting it to 10.0,
	// the graph won't zoom in further than a 10 MB/s range.
	// Dynamic Scaling: as the speed increases (e.g., to 100 MB/s), the addSpeedPoint logic
	// updates targetMax, and m_maxSpeed follows it using smoothing.
	// This allows the graph to "grow" vertically if the drive performs better than expected.
	inline double SPEED_GRAPH_MAX_SPEED = Defaults::SPEED_GRAPH_MAX_SPEED;

	// Files larger than this (in MB) will be forced to disk (fdatasync)
	// User Control: Users with high-speed NVMe drives might set the threshold to 0MB 
	// to verify everything from disk, while users with slow HDD/USB drives 
	// can set it higher to maintain responsiveness.
	inline int SYNC_THRESHOLD_MB = Defaults::SYNC_THRESHOLD_MB * 1024 * 1024;

	// Select files in file manager after copy
	inline bool SELECT_FILES_AFTER_COPY = Defaults::SELECT_FILES_AFTER_COPY;

	// ------------------- CopyWorker ---------------------------
	// 8MB is widely considered the peak performance point for high-speed I/O
	// Syscall Overhead: Every time you call read() or write(), the CPU has to switch
	// from "User Mode" to "Kernel Mode." If the buffer is too small (e.g., 4KB),
	// you waste massive amounts of CPU time just switching back and forth.
	// Disk Throughput: Mechanical drives and USB protocols prefer larger,
	// contiguous "bursts" of data.
	// CPU Cache: If the buffer is too large (e.g., 128MB), it won't fit in the
	// CPU's L3 cache, which can actually slow down the checksum calculation (XXH64_update).
	inline constexpr size_t BUFFER_SIZE = 8 * 1024 * 1024;

	// Interval at which the copy worker sends data to main thread
	inline constexpr double SPEED_UPDATE_INTERVAL = 0.05; // 50ms (20Hz)

	// 50MB default
	inline constexpr uintmax_t DISK_SPACE_SAFETY_MARGIN = 50 * 1024 * 1024;

	void load();
	void save();
} // namespace Config