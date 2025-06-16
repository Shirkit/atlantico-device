#ifndef DEVICE_MANAGER_H_
#define DEVICE_MANAGER_H_

#include "Config.h"
#include "Types.h"
#include "ModelManager.h"
#include "NetworkManager.h"
#include "StateMachine.h"
#include "TaskCoordinator.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

#if USE_ADVANCED_LOGGER
#include "Logger.h"
#endif

class DeviceManager {
private:
    DeviceConfig* deviceConfig;
    char* clientName;
    ModelManager* modelManager;
    NetworkManager* networkManager;
    
    // New state machine architecture
    StateMachine* stateMachine;
    TaskCoordinator* taskCoordinator;
    
    // Legacy state variables (for compatibility during transition)
    ModelState newModelState;
    FederateState federateState;
    int currentRound;
    bool waitingForMe;
    bool unsubscribeFromResume;
    
    // Memory tracking
    FixedMemoryUsage fixedMemoryUsage;
    RoundMemoryUsage roundMemoryUsage;
    
    // Timing
    unsigned long previousTransmit;
    unsigned long previousConstruct;

public:
    DeviceManager();
    ~DeviceManager();
    
    // Initialization
    void bootUp(bool initBaseModel = true);
    ErrorResult loadDeviceDefinitions();
    ErrorResult loadDeviceConfig();
    ErrorResult saveDeviceConfig();
    
    // State machine integration
    bool initializeStateMachine();
    bool startTaskCoordinator();
    void updateDeviceState();
    bool transitionToState(DeviceState newState);
    bool updateFederationStatus(FederateStatus newStatus);
    
    // Main processing (refactored to work with task coordinator)
    void processModel();
    void processMessages();
    
    // Federation control methods
    bool startFederationTraining();
    bool stopFederationTraining();
    bool handleFederationCommand(const char* command, JsonDocument& doc);
    void resetFederationState();
    
    // Memory and debugging
    void printMemory();
    void printTiming(bool doReset = false);
    void captureOnBootMemory();
    void captureAfterFullSetupMemory();
    
    // Getters
    ModelManager* getModelManager() { return modelManager; }
    NetworkManager* getNetworkManager() { return networkManager; }
    DeviceConfig* getDeviceConfig() { return deviceConfig; }
    const char* getClientName() { return clientName; }
    
    // State machine access
    StateMachine* getStateMachine() { return stateMachine; }
    TaskCoordinator* getTaskCoordinator() { return taskCoordinator; }
    
    // Legacy state getters (for compatibility)
    ModelState getNewModelState() { return newModelState; }
    FederateState getFederateState() { return federateState; }
    int getCurrentRound() { return currentRound; }
    
    // New state getters
    DeviceState getCurrentDeviceState() { return stateMachine ? stateMachine->getCurrentState() : DEVICE_INITIALIZING; }
    FederateStatus getCurrentFederateStatus() { return stateMachine ? stateMachine->getFederateStatus() : FEDERATE_NONE; }
    
    // Setters
    void setNewModelState(ModelState state) { newModelState = state; }
    void setFederateState(FederateState state) { federateState = state; }
    void setCurrentRound(int round) { currentRound = round; }
    
    // Callbacks for network events
    void onModelReceived(Stream& stream);
    void onRawModelReceived(Stream& stream);
    void onCommandReceived(Stream& stream);
    void onResumeReceived(Stream& stream);
    void onRawResumeReceived(Stream& stream);
};

// Global variables for memory tracking
extern multi_heap_info_t info;
extern unsigned long previousMillis;

#endif /* DEVICE_MANAGER_H_ */
