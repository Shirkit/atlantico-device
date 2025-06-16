#include "TaskCoordinator.h"
#include "ModelManager.h"
#include "NetworkManager.h"
#include "Config.h"
#include <Arduino.h>


#include "Logger.h"


TaskCoordinator::TaskCoordinator(StateMachine* sm, ModelManager* mm, NetworkManager* nm) :
    stateMachine(sm),
    modelManager(mm),
    networkManager(nm),
    sensorManager(nullptr),
    inferenceManager(nullptr),
    communicationTaskHandle(nullptr),
    federationTaskHandle(nullptr),
    inferenceTaskHandle(nullptr),
    sensorTaskHandle(nullptr),
    stateUpdateQueue(nullptr),
    federationDataQueue(nullptr),
    inferenceDataQueue(nullptr),
    sensorDataQueue(nullptr),
    stateMutex(nullptr),
    modelMutex(nullptr),
    networkMutex(nullptr),
    tasksCreated(false),
    coordinatorRunning(false)
{
    LOG_INFO("TaskCoordinator created with state machine and managers");
}

TaskCoordinator::~TaskCoordinator() {
    shutdown();
}

bool TaskCoordinator::initialize() {
    if (!createSemaphores()) {

        LOG_ERROR("Failed to create semaphores");

        return false;
    }
    
    if (!createQueues()) {

        LOG_ERROR("Failed to create queues");

        deleteSemaphores();
        return false;
    }
    

    LOG_INFO("TaskCoordinator initialized successfully");

    
    return true;
}

bool TaskCoordinator::startTasks() {
    if (tasksCreated) {

        LOG_WARN("Tasks already created");

        return true;
    }
    
    // Create Communication Task (Highest Priority)
    BaseType_t result = xTaskCreate(
        communicationTaskFunction,
        "CommTask",
        COMMUNICATION_STACK_SIZE,
        this,
        COMMUNICATION_PRIORITY,
        &communicationTaskHandle
    );
    
    if (result != pdPASS) {

        LOG_ERROR("Failed to create communication task");

        return false;
    }
    
    // Create Federation Task
    result = xTaskCreate(
        federationTaskFunction,
        "FedTask",
        FEDERATION_STACK_SIZE,
        this,
        FEDERATION_PRIORITY,
        &federationTaskHandle
    );
    
    if (result != pdPASS) {

        LOG_ERROR("Failed to create federation task");

        return false;
    }
    
    // Create Inference Task
    result = xTaskCreate(
        inferenceTaskFunction,
        "InfTask",
        INFERENCE_STACK_SIZE,
        this,
        INFERENCE_PRIORITY,
        &inferenceTaskHandle
    );
    
    if (result != pdPASS) {

        LOG_ERROR("Failed to create inference task");

        return false;
    }
    
    // Create Sensor Task (Lowest Priority)
    result = xTaskCreate(
        sensorTaskFunction,
        "SensorTask",
        SENSOR_STACK_SIZE,
        this,
        SENSOR_PRIORITY,
        &sensorTaskHandle
    );
    
    if (result != pdPASS) {

        LOG_ERROR( "TaskCoordinator", "Failed to create sensor task");

        return false;
    }
    
    tasksCreated = true;
    coordinatorRunning = true;
    

    LOG_INFO( "TaskCoordinator", "All tasks created successfully");

    
    return true;
}

void TaskCoordinator::stopTasks() {
    coordinatorRunning = false;
    
    if (communicationTaskHandle) {
        vTaskDelete(communicationTaskHandle);
        communicationTaskHandle = nullptr;
    }
    
    if (federationTaskHandle) {
        vTaskDelete(federationTaskHandle);
        federationTaskHandle = nullptr;
    }
    
    if (inferenceTaskHandle) {
        vTaskDelete(inferenceTaskHandle);
        inferenceTaskHandle = nullptr;
    }
    
    if (sensorTaskHandle) {
        vTaskDelete(sensorTaskHandle);
        sensorTaskHandle = nullptr;
    }
    
    tasksCreated = false;
    

    LOG_INFO( "TaskCoordinator", "All tasks stopped");

}

void TaskCoordinator::shutdown() {
    stopTasks();
    deleteQueues();
    deleteSemaphores();
    

    LOG_INFO( "TaskCoordinator", "Shutdown complete");

}

void TaskCoordinator::updateTaskStates() {
    if (!stateMachine) return;
    
    DeviceState currentState = stateMachine->getCurrentState();
    
    switch (currentState) {
        case DEVICE_INITIALIZING:
            // Only communication task should run during initialization
            suspendInferenceTask();
            suspendFederationTask();
            break;
            
        case INFERENCE_MODE:
            // Inference and sensor tasks can run, federation suspended
            resumeInferenceTask();
            suspendFederationTask();
            break;
            
        case FEDERATION_TRAINING:
            // Federation has priority, suspend inference
            resumeFederationTask();
            suspendInferenceTask();
            break;
            
        case FEDERATION_RECOVERY:
            // Only federation task for recovery, inference suspended
            resumeFederationTask();
            suspendInferenceTask();
            break;
            
        case DEVICE_ERROR:
            // Suspend all tasks except communication for error reporting
            suspendInferenceTask();
            suspendFederationTask();
            break;
    }
    
    // Send state update to all tasks
    sendStateUpdate(currentState);
}

void TaskCoordinator::suspendInferenceTask() {
    if (inferenceTaskHandle) {
        vTaskSuspend(inferenceTaskHandle);
    }
}

void TaskCoordinator::resumeInferenceTask() {
    if (inferenceTaskHandle) {
        vTaskResume(inferenceTaskHandle);
    }
}

void TaskCoordinator::suspendFederationTask() {
    if (federationTaskHandle) {
        vTaskSuspend(federationTaskHandle);
    }
}

void TaskCoordinator::resumeFederationTask() {
    if (federationTaskHandle) {
        vTaskResume(federationTaskHandle);
    }
}

bool TaskCoordinator::sendStateUpdate(DeviceState newState) {
    StateUpdateMessage msg;
    msg.newState = newState;
    msg.newStatus = stateMachine ? stateMachine->getFederateStatus() : FEDERATE_NONE;
    msg.timestamp = millis();
    
    if (stateUpdateQueue) {
        return xQueueSend(stateUpdateQueue, &msg, pdMS_TO_TICKS(100)) == pdTRUE;
    }
    return false;
}

bool TaskCoordinator::sendFederationData(const void* data, size_t size) {
    FederationDataMessage msg;
    msg.type = FederationDataMessage::MODEL_UPDATE;
    msg.data = const_cast<void*>(data);
    msg.dataSize = size;
    msg.timestamp = millis();
    
    if (federationDataQueue) {
        return xQueueSend(federationDataQueue, &msg, pdMS_TO_TICKS(100)) == pdTRUE;
    }
    return false;
}

bool TaskCoordinator::sendInferenceData(const void* data, size_t size) {
    InferenceDataMessage msg;
    msg.type = InferenceDataMessage::INFERENCE_REQUEST;
    msg.data = const_cast<void*>(data);
    msg.dataSize = size;
    msg.timestamp = millis();
    
    if (inferenceDataQueue) {
        return xQueueSend(inferenceDataQueue, &msg, pdMS_TO_TICKS(100)) == pdTRUE;
    }
    return false;
}

bool TaskCoordinator::sendSensorData(const void* data, size_t size) {
    SensorDataMessage msg;
    msg.type = SensorDataMessage::SENSOR_READING;
    msg.data = const_cast<void*>(data);
    msg.dataSize = size;
    msg.timestamp = millis();
    
    if (sensorDataQueue) {
        return xQueueSend(sensorDataQueue, &msg, pdMS_TO_TICKS(100)) == pdTRUE;
    }
    return false;
}

bool TaskCoordinator::requestModelAccess(TaskHandle_t requestingTask) {
    if (modelMutex) {
        return xSemaphoreTake(modelMutex, pdMS_TO_TICKS(1000)) == pdTRUE;
    }
    return false;
}

void TaskCoordinator::releaseModelAccess(TaskHandle_t requestingTask) {
    if (modelMutex) {
        xSemaphoreGive(modelMutex);
    }
}

bool TaskCoordinator::requestNetworkAccess(TaskHandle_t requestingTask) {
    if (networkMutex) {
        return xSemaphoreTake(networkMutex, pdMS_TO_TICKS(1000)) == pdTRUE;
    }
    return false;
}

void TaskCoordinator::releaseNetworkAccess(TaskHandle_t requestingTask) {
    if (networkMutex) {
        xSemaphoreGive(networkMutex);
    }
}

bool TaskCoordinator::areTasksHealthy() {
    // Reduce debug spam - only log health checks every 60 seconds
    static unsigned long lastDebugTime = 0;
    unsigned long currentTime = millis();
    bool shouldDebug = (currentTime - lastDebugTime) > 60000;
    
    if (shouldDebug) {
        LOG_DEBUG("Checking task health - tasksCreated: %s", tasksCreated ? "true" : "false");
    }
    
    if (!tasksCreated) {
        LOG_ERROR("Tasks not created yet - marking as unhealthy");
        return false;
    }
    
    // Check if task handles are valid
    if (!communicationTaskHandle || !federationTaskHandle || !inferenceTaskHandle || !sensorTaskHandle) {
        LOG_ERROR("One or more task handles are NULL:");
        LOG_ERROR("Comm: %p, Fed: %p, Inf: %p, Sensor: %p", 
                  communicationTaskHandle, federationTaskHandle, inferenceTaskHandle, sensorTaskHandle);
        return false;
    }
    
    // Check if tasks are still running and have sufficient stack space
    UBaseType_t commWaterMark = getTaskHighWaterMark(communicationTaskHandle);
    UBaseType_t fedWaterMark = getTaskHighWaterMark(federationTaskHandle);
    UBaseType_t infWaterMark = getTaskHighWaterMark(inferenceTaskHandle);
    UBaseType_t sensorWaterMark = getTaskHighWaterMark(sensorTaskHandle);
    
    if (shouldDebug) {
        LOG_DEBUG("Stack watermarks - Comm: %d, Fed: %d, Inf: %d, Sensor: %d", 
                  (int)commWaterMark, (int)fedWaterMark, (int)infWaterMark, (int)sensorWaterMark);
        lastDebugTime = currentTime;
    }
    
    // Consider tasks unhealthy if they have less than 5% stack remaining (was 10%)
    const UBaseType_t minCommStack = COMMUNICATION_STACK_SIZE * 0.05;
    const UBaseType_t minFedStack = FEDERATION_STACK_SIZE * 0.05;
    const UBaseType_t minInfStack = INFERENCE_STACK_SIZE * 0.05;
    const UBaseType_t minSensorStack = SENSOR_STACK_SIZE * 0.05;
    
    bool healthy = (commWaterMark > minCommStack &&
                   fedWaterMark > minFedStack &&
                   infWaterMark > minInfStack &&
                   sensorWaterMark > minSensorStack);
    
    // Debug unhealthy tasks
    if (!healthy) {
        LOG_ERROR("UNHEALTHY TASKS DETECTED:");
        LOG_ERROR("Comm: %d/%d (min: %d) %s", (int)commWaterMark, COMMUNICATION_STACK_SIZE, (int)minCommStack, 
                  commWaterMark > minCommStack ? "OK" : "FAIL");
        LOG_ERROR("Fed: %d/%d (min: %d) %s", (int)fedWaterMark, FEDERATION_STACK_SIZE, (int)minFedStack,
                  fedWaterMark > minFedStack ? "OK" : "FAIL");
        LOG_ERROR("Inf: %d/%d (min: %d) %s", (int)infWaterMark, INFERENCE_STACK_SIZE, (int)minInfStack,
                  infWaterMark > minInfStack ? "OK" : "FAIL");
        LOG_ERROR("Sensor: %d/%d (min: %d) %s", (int)sensorWaterMark, SENSOR_STACK_SIZE, (int)minSensorStack,
                  sensorWaterMark > minSensorStack ? "OK" : "FAIL");
    } else {
        // Reduce debug spam - only log every 60 seconds when healthy
        static unsigned long lastHealthyLog = 0;
        unsigned long currentTime = millis();
        if ((currentTime - lastHealthyLog) > 60000) {
            LOG_DEBUG("All tasks are healthy");
            lastHealthyLog = currentTime;
        }
    }
    
    return healthy;
}

void TaskCoordinator::printTaskStatus() {
    LOG_INFO("TaskCoordinator - Stack remaining - Comm: %d, Fed: %d, Inf: %d, Sensor: %d",
             (int)getTaskHighWaterMark(communicationTaskHandle),
             (int)getTaskHighWaterMark(federationTaskHandle),
             (int)getTaskHighWaterMark(inferenceTaskHandle),
             (int)getTaskHighWaterMark(sensorTaskHandle));
}

UBaseType_t TaskCoordinator::getTaskHighWaterMark(TaskHandle_t task) {
    if (task) {
        return uxTaskGetStackHighWaterMark(task);
    }
    return 0;
}

// Static task function implementations
void TaskCoordinator::communicationTaskFunction(void* parameters) {
    TaskCoordinator* coordinator = static_cast<TaskCoordinator*>(parameters);
    coordinator->runCommunicationTask();
}

void TaskCoordinator::federationTaskFunction(void* parameters) {
    TaskCoordinator* coordinator = static_cast<TaskCoordinator*>(parameters);
    coordinator->runFederationTask();
}

void TaskCoordinator::inferenceTaskFunction(void* parameters) {
    TaskCoordinator* coordinator = static_cast<TaskCoordinator*>(parameters);
    coordinator->runInferenceTask();
}

void TaskCoordinator::sensorTaskFunction(void* parameters) {
    TaskCoordinator* coordinator = static_cast<TaskCoordinator*>(parameters);
    coordinator->runSensorTask();
}

// Task implementation methods
void TaskCoordinator::runCommunicationTask() {
    StateUpdateMessage stateMsg;
    

    LOG_INFO( "CommunicationTask", "Started");

    
    while (coordinatorRunning) {
        // Check for state updates
        if (xQueueReceive(stateUpdateQueue, &stateMsg, pdMS_TO_TICKS(100)) == pdTRUE) {
            // Handle state change - update network behavior based on new state
            handleStateTransition(stateMsg.newState);
        }
        
        // Handle network communication
        if (networkManager && requestNetworkAccess(communicationTaskHandle)) {
            networkManager->processMessages();
            releaseNetworkAccess(communicationTaskHandle);
        }
        
        // Short delay to prevent task starvation
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    

    LOG_INFO( "CommunicationTask", "Terminated");

    vTaskDelete(nullptr);
}

void TaskCoordinator::runFederationTask() {
    FederationDataMessage fedMsg;
    

    LOG_INFO( "FederationTask", "Started");

    
    while (coordinatorRunning) {
        // Only run if federation is active
        if (stateMachine && stateMachine->isFederationActive()) {
            // Check for federation data
            if (xQueueReceive(federationDataQueue, &fedMsg, pdMS_TO_TICKS(100)) == pdTRUE) {
                // Process federation message
                processFederationMessage(fedMsg);
            }
            
            // Handle federation training
            if (modelManager && requestModelAccess(federationTaskHandle)) {
                // Execute federation training logic based on DeviceManager.processModel()
                executeFederationTraining();
                releaseModelAccess(federationTaskHandle);
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    

    LOG_INFO( "FederationTask", "Terminated");

    vTaskDelete(nullptr);
}

void TaskCoordinator::runInferenceTask() {
    InferenceDataMessage infMsg;
    

    LOG_INFO( "InferenceTask", "Started");

    
    while (coordinatorRunning) {
        // Only run if inference is allowed
        if (stateMachine && stateMachine->isInferenceAllowed()) {
            // Check for inference requests
            if (xQueueReceive(inferenceDataQueue, &infMsg, pdMS_TO_TICKS(100)) == pdTRUE) {
                // Process inference request
                if (inferenceManager) {
                    // TODO: Implement inference logic
                }
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    

    LOG_INFO( "InferenceTask", "Terminated");

    vTaskDelete(nullptr);
}

void TaskCoordinator::runSensorTask() {
    SensorDataMessage sensorMsg;
    

    LOG_INFO( "SensorTask", "Started");

    
    while (coordinatorRunning) {
        // Sensors can run in most states
        if (stateMachine && StateMachine::allowsSensorOperation(stateMachine->getCurrentState())) {
            if (sensorManager) {
                // TODO: Implement sensor data collection
                // Smart buffering during federation training
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    

    LOG_INFO( "SensorTask", "Terminated");

    vTaskDelete(nullptr);
}

// Helper methods
bool TaskCoordinator::createQueues() {
    stateUpdateQueue = xQueueCreate(QUEUE_SIZE, sizeof(StateUpdateMessage));
    federationDataQueue = xQueueCreate(QUEUE_SIZE, sizeof(FederationDataMessage));
    inferenceDataQueue = xQueueCreate(QUEUE_SIZE, sizeof(InferenceDataMessage));
    sensorDataQueue = xQueueCreate(QUEUE_SIZE, sizeof(SensorDataMessage));
    
    return (stateUpdateQueue && federationDataQueue && 
            inferenceDataQueue && sensorDataQueue);
}

bool TaskCoordinator::createSemaphores() {
    stateMutex = xSemaphoreCreateMutex();
    modelMutex = xSemaphoreCreateMutex();
    networkMutex = xSemaphoreCreateMutex();
    
    return (stateMutex && modelMutex && networkMutex);
}

void TaskCoordinator::deleteQueues() {
    if (stateUpdateQueue) {
        vQueueDelete(stateUpdateQueue);
        stateUpdateQueue = nullptr;
    }
    if (federationDataQueue) {
        vQueueDelete(federationDataQueue);
        federationDataQueue = nullptr;
    }
    if (inferenceDataQueue) {
        vQueueDelete(inferenceDataQueue);
        inferenceDataQueue = nullptr;
    }
    if (sensorDataQueue) {
        vQueueDelete(sensorDataQueue);
        sensorDataQueue = nullptr;
    }
}

void TaskCoordinator::deleteSemaphores() {
    if (stateMutex) {
        vSemaphoreDelete(stateMutex);
        stateMutex = nullptr;
    }
    if (modelMutex) {
        vSemaphoreDelete(modelMutex);
        modelMutex = nullptr;
    }
    if (networkMutex) {
        vSemaphoreDelete(networkMutex);
        networkMutex = nullptr;
    }
}

void TaskCoordinator::handleStateTransition(DeviceState newState) {
    // Add cooldown to prevent spam logging
    static DeviceState lastState = DEVICE_INITIALIZING;
    static unsigned long lastTransitionTime = 0;
    unsigned long currentTime = millis();
    
    // Only log if state actually changed or if it's been more than 30 seconds
    if (newState != lastState || (currentTime - lastTransitionTime) > 30000) {
        // Update task behavior based on new state
        updateTaskStates();
        
        LOG_INFO("TaskCoordinator - Handled state transition to %s", 
                 stateMachine ? stateMachine->getStateString() : "UNKNOWN");
        
        lastState = newState;
        lastTransitionTime = currentTime;
    }
}

void TaskCoordinator::processFederationMessage(const FederationDataMessage& fedMsg) {
    LOG_DEBUG("Processing federation message type: %d, size: %d", fedMsg.type, fedMsg.dataSize);
    
    switch (fedMsg.type) {
        case FederationDataMessage::MODEL_UPDATE:
            LOG_INFO("Processing MODEL_UPDATE federation message");
            // Handle model update - this would come from network callbacks
            // The data should contain model information to process
            if (fedMsg.data && fedMsg.dataSize > 0) {
                // In a real implementation, this would parse the model data
                // For now, we acknowledge the message
                LOG_INFO("Received model update data (%d bytes)", fedMsg.dataSize);
            }
            break;
            
        case FederationDataMessage::TRAINING_COMPLETE:
            LOG_INFO("Processing TRAINING_COMPLETE federation message");
            // Handle training completion notification
            // This could trigger state transitions or cleanup
            break;
            
        case FederationDataMessage::ERROR_OCCURRED:
            LOG_ERROR("Processing ERROR_OCCURRED federation message");
            // Handle federation errors - may need to reset state
            if (stateMachine) {
                stateMachine->reportError();
            }
            break;
            
        default:
            LOG_WARN("Unknown federation message type: %d", fedMsg.type);
            break;
    }
}

void TaskCoordinator::executeFederationTraining() {
    // This implements the core federation training logic from DeviceManager.processModel()
    // Only execute if we have access to the DeviceManager (through a manager reference)
    
    // Get current model state from DeviceManager
    ModelState currentModelState = ModelState_IDLE;
    
    // We need access to DeviceManager to get the model state
    // For now, we'll implement a basic version that could be enhanced
    // with proper DeviceManager integration
    
    if (!modelManager) {
        LOG_ERROR("No ModelManager available for federation training");
        return;
    }
    
    // Check if we have a federation model ready to train
    auto federationModel = modelManager->getNewModel();
    auto federateConfig = modelManager->getFederateModelConfig();
    
    if (!federationModel || !federateConfig) {
        LOG_DEBUG("No federation model or config available for training");
        return;
    }
    
    LOG_INFO("Executing federation training logic");
    
    // Implementation based on DeviceManager.processModel() switch statement
    // Case: ModelState_READY_TO_TRAIN
    if (federationModel) {
        LOG_INFO("Training federation model with federated configuration");
        
        // Memory tracking (equivalent to DeviceManager heap_caps_get_info calls)
        multi_heap_info_t info;
        heap_caps_get_info(&info, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        size_t beforeTrain = info.total_free_bytes;
        
        // Train the model using ModelManager
        auto metrics = modelManager->trainModelFromOriginalDataset(
            *federationModel,
            *federateConfig,
            X_TRAIN_PATH,
            Y_TRAIN_PATH
        );
        
        if (metrics) {
            modelManager->setNewModelMetrics(metrics);
            
            // Memory tracking after training
            heap_caps_get_info(&info, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            size_t afterTrain = info.total_free_bytes;
            
            LOG_INFO("Federation training completed - Memory usage: %d -> %d bytes", 
                     beforeTrain, afterTrain);
            
            // Signal training completion via federation data queue
            FederationDataMessage completionMsg;
            completionMsg.type = FederationDataMessage::TRAINING_COMPLETE;
            completionMsg.data = nullptr;
            completionMsg.dataSize = 0;
            completionMsg.timestamp = millis();
            
            // Send completion message back to the queue for processing
            if (federationDataQueue) {
                xQueueSend(federationDataQueue, &completionMsg, pdMS_TO_TICKS(10));
            }
            
            // Update federation status if we have access to DeviceManager
            // This would normally be done through DeviceManager::updateFederationStatus
            LOG_INFO("Federation training round completed successfully");
            
        } else {
            LOG_ERROR("Federation training failed - no metrics returned");
            
            // Signal error via federation data queue
            FederationDataMessage errorMsg;
            errorMsg.type = FederationDataMessage::ERROR_OCCURRED;
            errorMsg.data = nullptr;
            errorMsg.dataSize = 0;
            errorMsg.timestamp = millis();
            
            if (federationDataQueue) {
                xQueueSend(federationDataQueue, &errorMsg, pdMS_TO_TICKS(10));
            }
        }
    }
}