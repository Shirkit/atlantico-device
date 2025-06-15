#include "Logger.h"
#include <stdarg.h>
#include <stdio.h>

// Static member definitions
LogLevel Logger::currentLevel = LOG_LEVEL_INFO;
bool Logger::remoteLoggingEnabled = false;
void* Logger::networkManager = nullptr;

void Logger::init(LogLevel level) {
    currentLevel = level;
    remoteLoggingEnabled = false;
    networkManager = nullptr;
    
    #if DEBUG
    Serial.begin(57600);
    LOG_INFO("Logger initialized with level: %s", logLevelToString(level));
    #endif
}

void Logger::setLevel(LogLevel level) {
    currentLevel = level;
    LOG_INFO("Log level changed to: %s", logLevelToString(level));
}

void Logger::enableRemoteLogging(void* netMgr) {
    networkManager = netMgr;
    remoteLoggingEnabled = true;
    LOG_INFO("Remote logging enabled");
}

void Logger::disableRemoteLogging() {
    remoteLoggingEnabled = false;
    networkManager = nullptr;
    LOG_INFO("Remote logging disabled");
}

void Logger::log(LogLevel level, const char* file, int line, const char* function, const char* format, ...) {
    #if DEBUG
    if (level > currentLevel) return;
    
    // Get timestamp
    unsigned long timestamp = millis();
    
    // Extract filename from full path
    const char* filename = strrchr(file, '/');
    if (filename) {
        filename++; // Skip the '/'
    } else {
        filename = file;
    }
    
    // Prepare log message
    char buffer[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    // Print to Serial (plain text, colors handled by monitor filter)
    const char* levelStr;
    switch (level) {
        case LOG_LEVEL_ERROR: levelStr = "ERROR"; break;
        case LOG_LEVEL_WARN:  levelStr = "WARN "; break;
        case LOG_LEVEL_INFO:  levelStr = "INFO "; break;
        case LOG_LEVEL_DEBUG: levelStr = "DEBUG"; break;
        case LOG_LEVEL_TRACE: levelStr = "TRACE"; break;
        default: levelStr = "     "; break;
    }
    
    Serial.printf("[%lu][%s][%s:%d][%s] %s\n", 
                  timestamp, 
                  levelStr, 
                  filename, 
                  line, 
                  function, 
                  buffer);
    
    // Send critical messages to server via remote logging
    if (remoteLoggingEnabled && networkManager && level <= LOG_LEVEL_WARN) {
        sendLogToServer(level, buffer);
    }
    #endif
}

void Logger::logError(ErrorCode code, const char* file, int line, const char* function, const char* format, ...) {
    #if DEBUG
    // Extract filename from full path
    const char* filename = strrchr(file, '/');
    if (filename) {
        filename++; // Skip the '/'
    } else {
        filename = file;
    }
    
    // Prepare error message
    char buffer[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    // Print error with code (plain text, colors handled by monitor filter)
    unsigned long timestamp = millis();
    Serial.printf("[%lu][ERROR][%s:%d][%s] [Code:%d-%s] %s\n", 
                  timestamp, 
                  filename, 
                  line, 
                  function,
                  code,
                  errorCodeToString(code),
                  buffer);
    
    // Always send errors to remote logging if enabled
    if (remoteLoggingEnabled && networkManager) {
        sendLogToServer(LOG_LEVEL_ERROR, buffer);
    }
    #endif
}

void Logger::sendLogToServer(LogLevel level, const char* message) {
    // Only send ERROR and WARN level messages to server
    if (level > LOG_LEVEL_WARN || !remoteLoggingEnabled || !networkManager) {
        return;
    }
    
    // This method will be called from within the Logger class
    // For now, we'll implement a simple approach that doesn't require NetworkManager inclusion
    // The actual MQTT sending will be handled by NetworkManager when it's properly integrated
    
    // TODO: Implement MQTT message sending via NetworkManager
    // For now, just log that we would send this remotely
    #if DEBUG
    Serial.printf("[REMOTE_LOG][%s] %s\n", logLevelToString(level), message);
    #endif
}

const char* Logger::errorCodeToString(ErrorCode code) {
    switch (code) {
        case ERR_SUCCESS: return "SUCCESS";
        case ERR_UNKNOWN: return "UNKNOWN";
        
        // File system errors
        case ERR_FS_MOUNT_FAILED: return "FS_MOUNT_FAILED";
        case ERR_FS_FILE_NOT_FOUND: return "FS_FILE_NOT_FOUND";
        case ERR_FS_FILE_READ_FAILED: return "FS_FILE_READ_FAILED";
        case ERR_FS_FILE_WRITE_FAILED: return "FS_FILE_WRITE_FAILED";
        case ERR_FS_JSON_PARSE_FAILED: return "FS_JSON_PARSE_FAILED";
        
        // Network errors
        case ERR_WIFI_CONNECTION_FAILED: return "WIFI_CONNECTION_FAILED";
        case ERR_MQTT_CONNECTION_FAILED: return "MQTT_CONNECTION_FAILED";
        case ERR_MQTT_PUBLISH_FAILED: return "MQTT_PUBLISH_FAILED";
        case ERR_MQTT_SUBSCRIBE_FAILED: return "MQTT_SUBSCRIBE_FAILED";
        case ERR_NETWORK_TIMEOUT: return "NETWORK_TIMEOUT";
        
        // Model errors
        case ERR_MODEL_LOAD_FAILED: return "MODEL_LOAD_FAILED";
        case ERR_MODEL_SAVE_FAILED: return "MODEL_SAVE_FAILED";
        case ERR_MODEL_TRAINING_FAILED: return "MODEL_TRAINING_FAILED";
        case ERR_MODEL_INVALID_CONFIG: return "MODEL_INVALID_CONFIG";
        case ERR_MODEL_MEMORY_ALLOCATION: return "MODEL_MEMORY_ALLOCATION";
        
        // Memory errors
        case ERR_OUT_OF_MEMORY: return "OUT_OF_MEMORY";
        case ERR_MEMORY_FRAGMENTATION: return "MEMORY_FRAGMENTATION";
        
        // Configuration errors
        case ERR_CONFIG_INVALID: return "CONFIG_INVALID";
        case ERR_CONFIG_MISSING_REQUIRED: return "CONFIG_MISSING_REQUIRED";
        case ERR_CONFIG_PARSE_FAILED: return "CONFIG_PARSE_FAILED";
        
        default: return "UNKNOWN_ERROR_CODE";
    }
}

const char* Logger::logLevelToString(LogLevel level) {
    switch (level) {
        case LOG_LEVEL_ERROR: return "ERROR";
        case LOG_LEVEL_WARN:  return "WARN";
        case LOG_LEVEL_INFO:  return "INFO";
        case LOG_LEVEL_DEBUG: return "DEBUG";
        case LOG_LEVEL_TRACE: return "TRACE";
        default: return "UNKNOWN";
    }
}
