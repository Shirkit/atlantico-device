#ifndef TASK_COORDINATOR_H_
#define TASK_COORDINATOR_H_

#include "StateMachine.h"
#include "Config.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

// Forward declarations for managers
class ModelManager;
class NetworkManager;
class SensorManager;
class InferenceManager;

// Task message structures (declared before TaskCoordinator class)
struct StateUpdateMessage {
    DeviceState newState;
    FederateStatus newStatus;
    unsigned long timestamp;
};

struct FederationDataMessage {
    enum Type { MODEL_UPDATE, TRAINING_COMPLETE, ERROR_OCCURRED } type;
    void* data;
    size_t dataSize;
    unsigned long timestamp;
};

struct InferenceDataMessage {
    enum Type { INFERENCE_REQUEST, INFERENCE_RESULT } type;
    void* data;
    size_t dataSize;
    unsigned long timestamp;
};

struct SensorDataMessage {
    enum Type { SENSOR_READING, BUFFER_STATUS } type;
    void* data;
    size_t dataSize;
    unsigned long timestamp;
};

/**
 * @brief Coordinates the 4 concurrent FreeRTOS tasks based on state machine state
 * 
 * Tasks and their priorities:
 * 1. Communication Task (Priority 4 - Highest): Network communication, MQTT
 * 2. Federation Task (Priority 3): Model training, federation protocol  
 * 3. Inference Task (Priority 2): Local model inference
 * 4. Sensor Task (Priority 1 - Lowest): Sensor data collection and buffering
 * 
 * The TaskCoordinator uses the StateMachine state to enable/disable tasks
 * and coordinate resource allocation between them.
 */
class TaskCoordinator {
private:
    // Core dependencies
    StateMachine* stateMachine;
    ModelManager* modelManager;
    NetworkManager* networkManager;
    SensorManager* sensorManager;
    InferenceManager* inferenceManager;
    
    // Task handles
    TaskHandle_t communicationTaskHandle;
    TaskHandle_t federationTaskHandle;
    TaskHandle_t inferenceTaskHandle;
    TaskHandle_t sensorTaskHandle;
    
    // Inter-task communication
    QueueHandle_t stateUpdateQueue;
    QueueHandle_t federationDataQueue;
    QueueHandle_t inferenceDataQueue;
    QueueHandle_t sensorDataQueue;
    
    // Synchronization primitives
    SemaphoreHandle_t stateMutex;
    SemaphoreHandle_t modelMutex;
    SemaphoreHandle_t networkMutex;
    
    // Task configuration
    static const int COMMUNICATION_STACK_SIZE = 8192;  // Increased for network operations
    static const int FEDERATION_STACK_SIZE = 12288;    // Increased for ML training operations  
    static const int INFERENCE_STACK_SIZE = 8192;      // Increased from 1024 to 4096
    static const int SENSOR_STACK_SIZE = 8192;         // Increased to match communication task stack
    
    static const int COMMUNICATION_PRIORITY = 4;
    static const int FEDERATION_PRIORITY = 3;
    static const int INFERENCE_PRIORITY = 2;
    static const int SENSOR_PRIORITY = 1;
    
    static const int QUEUE_SIZE = 10;
    
    // Task management
    bool tasksCreated;
    bool coordinatorRunning;
    
public:
    TaskCoordinator(StateMachine* sm, ModelManager* mm, NetworkManager* nm);
    ~TaskCoordinator();
    
    // Lifecycle management
    bool initialize();
    bool startTasks();
    void stopTasks();
    void shutdown();
    
    // Manager registration (for dependency injection)
    void setSensorManager(SensorManager* sm) { sensorManager = sm; }
    void setInferenceManager(InferenceManager* im) { inferenceManager = im; }
    
    // State-based task control
    void updateTaskStates();
    void suspendInferenceTask();
    void resumeInferenceTask();
    void suspendFederationTask();
    void resumeFederationTask();
    
    // Inter-task communication
    bool sendStateUpdate(DeviceState newState);
    bool sendFederationData(const void* data, size_t size);
    bool sendInferenceData(const void* data, size_t size);
    bool sendSensorData(const void* data, size_t size);
    
    // Resource coordination
    bool requestModelAccess(TaskHandle_t requestingTask);
    void releaseModelAccess(TaskHandle_t requestingTask);
    bool requestNetworkAccess(TaskHandle_t requestingTask);
    void releaseNetworkAccess(TaskHandle_t requestingTask);
    
    // Task status monitoring
    bool areTasksHealthy();
    void printTaskStatus();
    UBaseType_t getTaskHighWaterMark(TaskHandle_t task);
    
    // Static task functions (required by FreeRTOS)
    static void communicationTaskFunction(void* parameters);
    static void federationTaskFunction(void* parameters);
    static void inferenceTaskFunction(void* parameters);
    static void sensorTaskFunction(void* parameters);
    
private:
    // Internal task implementations
    void runCommunicationTask();
    void runFederationTask();
    void runInferenceTask();
    void runSensorTask();
    
    // Helper methods
    bool createQueues();
    bool createSemaphores();
    void deleteQueues();
    void deleteSemaphores();
    bool shouldTaskRun(TaskHandle_t task);
    void handleStateTransition(DeviceState newState);
    
    // Federation processing methods
    void processFederationMessage(const FederationDataMessage& fedMsg);
    void executeFederationTraining();
};

#endif /* TASK_COORDINATOR_H_ */