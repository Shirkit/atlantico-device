#ifndef NETWORK_MANAGER_H_
#define NETWORK_MANAGER_H_

#include "Config.h"
#include "Types.h"
#include <WiFi.h>
#include <PicoMQTT.h>
#include <functional>

class NetworkManager {
private:
    PicoMQTT::Client* mqtt;
    bool sendingMessage;
    char* clientName;
    
    // Connection retry parameters
    static const int MAX_MQTT_RETRIES = 5;
    static const unsigned long MQTT_RETRY_DELAY = 2000; // 2 seconds
    static const unsigned long MQTT_CONNECTION_TIMEOUT = 10000; // 10 seconds

public:
    NetworkManager(const char* clientName);
    ~NetworkManager();
    
    // WiFi management
    bool connectToWifi(bool forever = true);
    bool isWifiConnected();
    
    // MQTT management
    bool connectToMQTT();
    bool isMQTTConnected();
    bool ensureConnected();
    void checkConnectionHealth(); // New method to diagnose connection issues
    
    // MQTT setup and messaging
    void setupMQTT(bool resume = false);
    void setupResume();
    void processMessages();
    
    // Command sending
    void sendMessageToNetwork(FederateCommand command, int currentRound = -1, ModelState newModelState = ModelState_IDLE);
    void sendModelToNetwork(NeuralNetwork& NN, multiClassClassifierMetrics& metrics, 
                           unsigned long datasetSize, unsigned long previousTransmit, 
                           unsigned long previousConstruct, const FixedMemoryUsage& fixedMemoryUsage,
                           const RoundMemoryUsage& roundMemoryUsage);
    void sendBinaryModelToNetwork(NeuralNetwork& NN);
    
    // Utility
    const char* modelStateToString(ModelState state);
    
    // Callbacks (to be set from outside)
    std::function<void(Stream&)> onModelReceived = nullptr;
    std::function<void(Stream&)> onRawModelReceived = nullptr;
    std::function<void(Stream&)> onCommandReceived = nullptr;
    std::function<void(Stream&)> onResumeReceived = nullptr;
    std::function<void(Stream&)> onRawResumeReceived = nullptr;
};

#endif /* NETWORK_MANAGER_H_ */
