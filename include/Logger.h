#ifndef LOGGER_H_
#define LOGGER_H_

#include "Config.h"
#include <Arduino.h>

// Log levels
enum LogLevel {
    LOG_LEVEL_ERROR = 0,
    LOG_LEVEL_WARN = 1,
    LOG_LEVEL_INFO = 2,
    LOG_LEVEL_DEBUG = 3,
    LOG_LEVEL_TRACE = 4
};

// Error codes for different subsystems
enum ErrorCode {
    // General errors
    ERR_SUCCESS = 0,
    ERR_UNKNOWN = 1,
    
    // File system errors
    ERR_FS_MOUNT_FAILED = 100,
    ERR_FS_FILE_NOT_FOUND = 101,
    ERR_FS_FILE_READ_FAILED = 102,
    ERR_FS_FILE_WRITE_FAILED = 103,
    ERR_FS_JSON_PARSE_FAILED = 104,
    ERR_FS_OPEN_FAILED = 105,
    ERR_FS_WRITE_FAILED = 106,
    
    // Network errors
    ERR_WIFI_CONNECTION_FAILED = 200,
    ERR_MQTT_CONNECTION_FAILED = 201,
    ERR_MQTT_PUBLISH_FAILED = 202,
    ERR_MQTT_SUBSCRIBE_FAILED = 203,
    ERR_NETWORK_TIMEOUT = 204,
    ERR_NETWORK_CONNECTION_FAILED = 205,
    ERR_NETWORK_PARSE_FAILED = 206,
    ERR_NETWORK_WIFI_CONNECTION_FAILED = 207,
    ERR_NETWORK_MQTT_CONNECTION_FAILED = 208,
    
    // Model errors
    ERR_MODEL_LOAD_FAILED = 300,
    ERR_MODEL_SAVE_FAILED = 301,
    ERR_MODEL_TRAINING_FAILED = 302,
    ERR_MODEL_INVALID_CONFIG = 303,
    ERR_MODEL_MEMORY_ALLOCATION = 304,
    
    // Memory errors
    ERR_OUT_OF_MEMORY = 400,
    ERR_MEMORY_FRAGMENTATION = 401,
    
    // Configuration errors
    ERR_CONFIG_INVALID = 500,
    ERR_CONFIG_MISSING_REQUIRED = 501,
    ERR_CONFIG_PARSE_FAILED = 502
};

// Error result structure
struct ErrorResult {
    ErrorCode code;
    const char* message;
    const char* file;
    int line;
    const char* function;
    ErrorCode errorCode; // For backward compatibility
    
    ErrorResult(ErrorCode c = ERR_SUCCESS, const char* msg = nullptr, 
                const char* f = nullptr, int l = 0, const char* func = nullptr)
        : code(c), message(msg), file(f), line(l), function(func), errorCode(c) {}
    
    bool isSuccess() const { return code == ERR_SUCCESS; }
    bool isError() const { return code != ERR_SUCCESS; }
    
    // Static factory methods
    static ErrorResult success() { return ErrorResult(ERR_SUCCESS, "Success"); }
    static ErrorResult error(ErrorCode c, const char* msg = nullptr) { return ErrorResult(c, msg); }
};

class Logger {
private:
    static LogLevel currentLevel;
    static bool remoteLoggingEnabled;
    static void* networkManager; // NetworkManager pointer for remote logging
    
public:
    static void init(LogLevel level = LOG_LEVEL_INFO);
    static void setLevel(LogLevel level);
    static void enableRemoteLogging(void* netMgr);
    static void disableRemoteLogging();
    
    // Core logging functions
    static void log(LogLevel level, const char* file, int line, const char* function, const char* format, ...);
    static void logError(ErrorCode code, const char* file, int line, const char* function, const char* format, ...);
    
    // Remote logging functions
    static void sendLogToServer(LogLevel level, const char* message);
    
    // Convenience functions
    static const char* errorCodeToString(ErrorCode code);
    static const char* logLevelToString(LogLevel level);
};

// Macro definitions for easy logging
#define LOG_LEVEL_ENABLED(level) (level <= Logger::currentLevel)

// Note: Logging macros are defined in Config.h to avoid duplication

#endif /* LOGGER_H_ */
