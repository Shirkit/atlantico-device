#ifndef SENSOR_MANAGER_H_
#define SENSOR_MANAGER_H_

#include "Config.h"
#include <Arduino.h>

#if USE_ADVANCED_LOGGER
#include "Logger.h"
#endif

/**
 * @brief Manages sensor data collection with smart buffering during federation training
 * 
 * This manager handles sensor operations and implements smart buffering strategies
 * to ensure data collection continues during federation training while managing
 * memory efficiently.
 */
class SensorManager {
private:
    // Sensor configuration
    bool sensorsInitialized;
    unsigned long lastSensorReading;
    unsigned long sensorInterval;
    
    // Smart buffering
    bool bufferingMode;
    size_t bufferSize;
    size_t bufferUsed;
    void* sensorBuffer;
    
    static const size_t DEFAULT_BUFFER_SIZE = 1024; // 1KB buffer
    static const unsigned long DEFAULT_SENSOR_INTERVAL = 1000; // 1 second
    
public:
    SensorManager();
    ~SensorManager();
    
    // Lifecycle management
    bool initialize();
    void shutdown();
    
    // Sensor operations
    bool readSensors();
    bool startBuffering();
    bool stopBuffering();
    void flushBuffer();
    
    // Data access
    size_t getBufferedDataSize() const { return bufferUsed; }
    const void* getBufferedData() const { return sensorBuffer; }
    bool hasNewData() const;
    
    // Configuration
    void setSensorInterval(unsigned long interval) { sensorInterval = interval; }
    void setBufferSize(size_t size);
    
    // Status
    bool isInitialized() const { return sensorsInitialized; }
    bool isBuffering() const { return bufferingMode; }
    unsigned long getLastReadingTime() const { return lastSensorReading; }
    
    // Debugging
    void printStatus() const;
};

#endif /* SENSOR_MANAGER_H_ */
