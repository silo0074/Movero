#ifndef DEBUGHELPER_H
#define DEBUGHELPER_H

#include <QDebug>
#include <QString>

// If CMake didn't define it, default to false
#ifndef DEBUG_ENABLED
    #define DEBUG_ENABLED 0
#endif

// Define log levels
enum class LogLevel {
    INFO,
    DEBUG,
    WARNING,
    ERROR
};

#if DEBUG_ENABLED == 1

    // This helper class handles the prefixing and auto-newline at the end of the line
    class LogHelper {
    public:
        LogHelper(LogLevel level, const char* file, int line) 
            : m_stream(qDebug().noquote()) {
            QString label;
            switch (level) {
                case LogLevel::INFO:    label = "[INFO]"; break;
                case LogLevel::DEBUG:   label = "[DEBUG]"; break;
                case LogLevel::WARNING: label = "[WARNING]"; break;
                case LogLevel::ERROR:   label = "[ERROR]"; break;
            }
            // Format the header: [LEVEL] (file:line)
            m_stream << label << "(" << file << ":" << line << ")";
        }

        // Return the internal QDebug stream to allow chaining with <<
        QDebug& stream() { return m_stream; }

    private:
        QDebug m_stream;
    };


    // Use the PROJECT_ROOT defined in CMakeLists.txt
    // If __FILE__ starts with PROJECT_ROOT, skip that many characters.
    // strncmp: This compares the beginning of the strings. 
    // If __FILE__ is /home/me/Movero/src/main.cpp and PROJECT_ROOT is 
    // /home/me/Movero/, the comparison is true, and it adds the length 
    // of the root to the pointer, leaving you with src/main.cpp
    // Ensure PROJECT_ROOT is handled as a string_view or pointer safely
    #define __FILENAME__ (strncmp(__FILE__, PROJECT_ROOT, strlen(PROJECT_ROOT)) == 0 ? \
                     &__FILE__[strlen(PROJECT_ROOT)] : __FILE__)

    #define LOG(level) LogHelper(level, __FILENAME__, __LINE__).stream()

#else

    // When disabled, LOG(level) evaluates to a "No-Op" (No Operation)
    // QNoDebug is a built-in Qt class that eats all stream inputs and does nothing
    #define LOG(level) QT_NO_QDEBUG_MACRO()

#endif


/*
Usage example:
#include "DebugHelper.h"
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
