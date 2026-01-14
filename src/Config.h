#pragma once
#include <QString>
#include <cstdint>

namespace Config {
    // Defined by CMakeLists
    // inline constexpr char APP_NAME[] = "Movero";
    // inline constexpr char APP_VERSION[] = "1.0.0";

    inline constexpr char DEVELOPER[] = "Liviu Istrate";
    
    // UI constants
    inline constexpr int SPEED_GRAPH_MIN_HEIGHT = 220;

    // Speed graph timer update 
    inline constexpr int UPDATE_INTERVAL_MS = 100;

    // 200 points will represent 20 seconds of history at 10Hz (200 * 0.1s)
    // History = SPEED_GRAPH_HISTORY_SIZE * (UPDATE_INTERVAL_MS / 1000.0)
    inline constexpr int SPEED_GRAPH_HISTORY_SIZE = 200;
    
    // m_maxSpeed represents the top value of the Y-Axis (the 100% height of the graph).
    // This is a "floor" or minimum scale. If you are copying a very small file at 2 MB/s, 
    // you don't want the graph to scale its max height to exactly 2 MB/s 
    // (which makes the graph look like it's "maxing out"). By setting it to 10.0, 
    // the graph won't zoom in further than a 10 MB/s range.
    // Dynamic Scaling: as the speed increases (e.g., to 100 MB/s), the addSpeedPoint logic 
    // updates targetMax, and m_maxSpeed follows it using smoothing. 
    // This allows the graph to "grow" vertically if the drive performs better than expected.
    inline constexpr double SPEED_GRAPH_MAX_SPEED = 10.0;

    // Set to true to bypass clipboard and test with a dummy file
    inline constexpr bool DRY_RUN = true;
    inline constexpr uintmax_t DRY_RUN_FILE_SIZE = 4ULL * 1024 * 1024 * 1024;

    // CopyWorker
    // 8MB is widely considered the peak performance point for high-speed I/O
    // Syscall Overhead: Every time you call read() or write(), the CPU has to switch 
    // from "User Mode" to "Kernel Mode." If the buffer is too small (e.g., 4KB), 
    // you waste massive amounts of CPU time just switching back and forth.
    // Disk Throughput: Mechanical drives and USB protocols prefer larger, 
    // contiguous "bursts" of data.
    // CPU Cache: If the buffer is too large (e.g., 128MB), it won't fit in the 
    // CPU's L3 cache, which can actually slow down the checksum calculation (XXH64_update).
    inline constexpr size_t BUFFER_SIZE = 8 * 1024 * 1024;
    inline constexpr double SPEED_UPDATE_INTERVAL = 0.05; // 50ms (20Hz)
    // 50MB default
    inline constexpr uintmax_t DISK_SPACE_SAFETY_MARGIN = 50 * 1024 * 1024;
}