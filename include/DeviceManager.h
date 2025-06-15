#ifndef DEVICE_MANAGER_H_
#define DEVICE_MANAGER_H_

#include "Config.h"
#include "Types.h"
#include "ModelManager.h"
#include "NetworkManager.h"
#include <LittleFS.h>

#if USE_ADVANCED_LOGGER
#include "Logger.h"
#endif

class DeviceManager {
private:
    DeviceConfig* deviceConfig;
    char* clientName;
    ModelManager* modelManager;
    NetworkManager* networkManager;
    
    // State variables
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
    
    // Main processing
    void processModel();
    void processMessages();
    
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
    ModelState getNewModelState() { return newModelState; }
    FederateState getFederateState() { return federateState; }
    int getCurrentRound() { return currentRound; }
    
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
