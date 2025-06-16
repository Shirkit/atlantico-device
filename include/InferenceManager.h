#ifndef INFERENCE_MANAGER_H_
#define INFERENCE_MANAGER_H_

#include "Config.h"
#include "Types.h"
#include <Arduino.h>

#if USE_ADVANCED_LOGGER
#include "Logger.h"
#endif

// Forward declaration
class ModelManager;

/**
 * @brief Manages local model inference operations
 * 
 * This manager handles inference requests on the local model when the device
 * is in INFERENCE_MODE. It coordinates with ModelManager to ensure model
 * availability and handles inference scheduling.
 */
class InferenceManager {
private:
    // Dependencies
    ModelManager* modelManager;
    
    // Inference state
    bool inferenceEnabled;
    bool modelLoaded;
    unsigned long lastInference;
    unsigned long inferenceCount;
    
    // Performance tracking
    unsigned long totalInferenceTime;
    unsigned long maxInferenceTime;
    unsigned long minInferenceTime;
    
    // Configuration
    static const unsigned long DEFAULT_INFERENCE_TIMEOUT = 5000; // 5 seconds
    
public:
    InferenceManager(ModelManager* mm = nullptr);
    ~InferenceManager();
    
    // Lifecycle management
    bool initialize();
    void shutdown();
    
    // Model management
    void setModelManager(ModelManager* mm) { modelManager = mm; }
    bool loadInferenceModel();
    void unloadInferenceModel();
    bool isModelReady() const { return modelLoaded; }
    
    // Inference operations
    bool runInference(const DFLOAT* inputData, size_t inputSize, 
                     DFLOAT* outputData, size_t outputSize);
    bool processInferenceRequest(const void* requestData, size_t requestSize);
    
    // State management
    void enableInference() { inferenceEnabled = true; }
    void disableInference() { inferenceEnabled = false; }
    bool isInferenceEnabled() const { return inferenceEnabled; }
    
    // Statistics
    unsigned long getInferenceCount() const { return inferenceCount; }
    unsigned long getAverageInferenceTime() const;
    unsigned long getMaxInferenceTime() const { return maxInferenceTime; }
    unsigned long getMinInferenceTime() const { return minInferenceTime; }
    unsigned long getLastInferenceTime() const { return lastInference; }
    
    // Performance monitoring
    void resetStatistics();
    void printStatistics() const;
    
    // Debugging
    void printStatus() const;
    
private:
    // Helper methods
    void updatePerformanceStats(unsigned long inferenceTime);
    bool validateInputData(const DFLOAT* inputData, size_t inputSize) const;
    bool validateOutputData(DFLOAT* outputData, size_t outputSize) const;
};

#endif /* INFERENCE_MANAGER_H_ */
