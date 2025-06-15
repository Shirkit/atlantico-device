#ifndef CONFIG_H_
#define CONFIG_H_

// Neural Network optimization macros - MUST be defined before any NeuralNetwork.h inclusion
#define NumberOf(arg) ((unsigned int)(sizeof(arg) / sizeof(arg[0])))
#define _2_OPTIMIZE 0B00100000  // MULTIPLE_BIASES_PER_LAYER

// Neural Network activation functions
#define ACTIVATION__PER_LAYER
#define Sigmoid
#define Tanh
#define ReLU
#define LeakyReLU
#define ELU
#define SELU
#define Softmax

// Debug settings
#define DEBUG 1

// Conditional Logger inclusion - enable for enhanced error handling
#define USE_ADVANCED_LOGGER 1

#if USE_ADVANCED_LOGGER
// Include the new logging system
#include "Logger.h"

// Set compile-time log level filtering (uncomment to reduce verbosity)
#define LOG_LEVEL_COMPILE_TIME LOG_LEVEL_INFO  // Only INFO and above (ERROR, WARN, INFO)
// #define LOG_LEVEL_COMPILE_TIME LOG_LEVEL_WARN  // Only WARN and above (ERROR, WARN)  
// #define LOG_LEVEL_COMPILE_TIME LOG_LEVEL_ERROR // Only ERROR messages
#endif

// Legacy macros for backward compatibility (will be replaced gradually)
#if DEBUG
#define D_SerialBegin(...) Serial.begin(__VA_ARGS__);
#define D_print(...)    Serial.print(__VA_ARGS__)
#define D_write(...)    Serial.write(__VA_ARGS__)
#define D_println(...)  Serial.println(__VA_ARGS__)
#define D_printf(...)   Serial.printf(__VA_ARGS__)
#else
#define D_SerialBegin(...)
#define D_print(...)
#define D_write(...)
#define D_println(...)
#define D_printf(...)
#endif

#if USE_ADVANCED_LOGGER
// New logging macros with compile-time filtering
#ifndef LOG_LEVEL_COMPILE_TIME
// Default: all levels enabled if not specified
#define LOG_ERROR(...)   Logger::log(LOG_LEVEL_ERROR, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define LOG_WARN(...)    Logger::log(LOG_LEVEL_WARN,  __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define LOG_INFO(...)    Logger::log(LOG_LEVEL_INFO,  __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define LOG_DEBUG(...)   Logger::log(LOG_LEVEL_DEBUG, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define LOG_TRACE(...)   Logger::log(LOG_LEVEL_TRACE, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#else
// Compile-time filtering based on LOG_LEVEL_COMPILE_TIME
#define LOG_ERROR(...)   Logger::log(LOG_LEVEL_ERROR, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#if LOG_LEVEL_COMPILE_TIME <= LOG_LEVEL_WARN
#define LOG_WARN(...)    Logger::log(LOG_LEVEL_WARN,  __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#else
#define LOG_WARN(...)    // Compiled out
#endif
#if LOG_LEVEL_COMPILE_TIME <= LOG_LEVEL_INFO
#define LOG_INFO(...)    Logger::log(LOG_LEVEL_INFO,  __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#else
#define LOG_INFO(...)    // Compiled out
#endif
#if LOG_LEVEL_COMPILE_TIME <= LOG_LEVEL_DEBUG
#define LOG_DEBUG(...)   Logger::log(LOG_LEVEL_DEBUG, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#else
#define LOG_DEBUG(...)   // Compiled out
#endif
#if LOG_LEVEL_COMPILE_TIME <= LOG_LEVEL_TRACE
#define LOG_TRACE(...)   Logger::log(LOG_LEVEL_TRACE, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#else
#define LOG_TRACE(...)   // Compiled out
#endif
#endif

#define LOG_ERROR_CODE(code, ...) Logger::logError(code, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)

#define RETURN_ERROR(code, ...) do { \
    LOG_ERROR_CODE(code, __VA_ARGS__); \
    return ErrorResult(code, #__VA_ARGS__); \
} while(0)

#define RETURN_ERROR_IF(condition, code, ...) \
    if (condition) { \
        LOG_ERROR_CODE(code, __VA_ARGS__); \
        return ErrorResult(code, #__VA_ARGS__); \
    }
#else
// Fallback to simple debug macros for now
#define LOG_ERROR(...)   D_printf(__VA_ARGS__); D_println("")
#define LOG_WARN(...)    D_printf(__VA_ARGS__); D_println("")
#define LOG_INFO(...)    D_printf(__VA_ARGS__); D_println("")
#define LOG_DEBUG(...)   D_printf(__VA_ARGS__); D_println("")
#define LOG_TRACE(...)   D_printf(__VA_ARGS__); D_println("")

#define LOG_ERROR_CODE(code, ...) D_printf(__VA_ARGS__); D_println("")

#define RETURN_ERROR_IF(condition, code, ...) \
    if (condition) { \
        D_printf(__VA_ARGS__); D_println(""); \
        return false; \
    }
#endif

// File paths
#define MODEL_PATH "/model.nn"
#define NEW_MODEL_PATH "/new_model.nn"
#define TEMPORARY_NEW_MODEL_PATH "/new_model_temp.nn"
#define CONFIGURATION_PATH "/config.json"
#define DEVICE_DEFINITION_PATH "/device.json"
#define X_TRAIN_PATH "/x_train.csv"
#define Y_TRAIN_PATH "/y_train.csv"
#define X_TEST_PATH "/x_test.csv"
#define Y_TEST_PATH "/y_test.csv"
#define GATHERED_DATA_PATH "/data.db"

// MQTT Topics
#define MQTT_PUBLISH_TOPIC "esp32/fl/model/push"
#define MQTT_RAW_PUBLISH_TOPIC "esp32/fl/model/rawpush"
#define MQTT_RECEIVE_TOPIC "esp32/fl/model/pull"
#define MQTT_RAW_RECEIVE_TOPIC "esp32/fl/model/rawpull"
#define MQTT_RESUME_TOPIC "esp32/fl/model/resume"
#define MQTT_RAW_RESUME_TOPIC "esp32/fl/model/rawresume"
#define MQTT_RECEIVE_COMMANDS_TOPIC "esp32/fl/commands/pull"
#define MQTT_SEND_COMMANDS_TOPIC "esp32/fl/commands/push"

// Network settings
#define WIFI_SSID "PedroRapha"
#define WIFI_PASSWORD "456123789a"
#define MQTT_BROKER "192.168.15.9"
#define CONNECTION_TIMEOUT 30000 // in milliseconds

// Neural Network settings
#if defined(USE_64_BIT_DOUBLE)
#define ARDUINOJSON_USE_DOUBLE 1
#else
#define ARDUINOJSON_USE_DOUBLE 0
#endif

#endif /* CONFIG_H_ */
