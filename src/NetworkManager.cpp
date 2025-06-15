#include "NetworkManager.h"
#include <ArduinoJson.h>
#include <LittleFS.h>

NetworkManager::NetworkManager(const char* clientName) {
    this->clientName = new char[strlen(clientName) + 1];
    strcpy(this->clientName, clientName);
    this->sendingMessage = false;
    this->mqtt = new PicoMQTT::Client(MQTT_BROKER, 1883, clientName, nullptr, nullptr, 5000UL, 30000UL, 10000UL);
}

NetworkManager::~NetworkManager() {
    delete[] clientName;
    delete mqtt;
}

bool NetworkManager::connectToWifi(bool forever) {
    LOG_INFO("Connecting to WiFi network: %s", WIFI_SSID);
    WiFi.config(IPAddress(192, 168, 15, 40 + String(clientName).substring(3).toInt()), 
                IPAddress(192, 168, 15, 1), IPAddress(255, 255, 255, 0));
    LOG_DEBUG("WiFi static IP configuration set");
    
    if (WiFi.status() == WL_CONNECTED) {
        LOG_INFO("Already connected to WiFi");
        LOG_INFO("IP address: %s", WiFi.localIP().toString().c_str());
        return true;
    } else {
        LOG_DEBUG("Starting WiFi connection process");
        
        // Disconnect and clear any existing configuration
        WiFi.disconnect(true);
        delay(1000);
        
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        unsigned long startTime = millis();
        
        while (WiFi.status() != WL_CONNECTED) {
            if (!forever && (millis() - startTime > CONNECTION_TIMEOUT)) {
                LOG_ERROR_CODE(ERR_NETWORK_WIFI_CONNECTION_FAILED, "WiFi connection timeout after %d ms", CONNECTION_TIMEOUT);
                // Log the WiFi status for debugging
                switch(WiFi.status()) {
                    case WL_NO_SSID_AVAIL:
                        LOG_ERROR("WiFi Error: SSID not available");
                        break;
                    case WL_CONNECT_FAILED:
                        LOG_ERROR("WiFi Error: Connection failed");
                        break;
                    case WL_CONNECTION_LOST:
                        LOG_ERROR("WiFi Error: Connection lost");
                        break;
                    case WL_DISCONNECTED:
                        LOG_ERROR("WiFi Error: Disconnected");
                        break;
                    default:
                        LOG_ERROR("WiFi Error: Unknown status %d", WiFi.status());
                        break;
                }
                return false;
            }
            delay(500);
            LOG_TRACE(".");
        }
        
        LOG_INFO("WiFi connected successfully!");
        LOG_INFO("IP address: %s", WiFi.localIP().toString().c_str());
        return true;
    }
}

bool NetworkManager::isWifiConnected() {
    return WiFi.status() == WL_CONNECTED;
}

bool NetworkManager::connectToMQTT() {
    LOG_INFO("Attempting MQTT connection to broker: %s", MQTT_BROKER);
    
    for (int attempt = 1; attempt <= MAX_MQTT_RETRIES; attempt++) {
        LOG_DEBUG("MQTT connection attempt %d/%d", attempt, MAX_MQTT_RETRIES);
        
        // Disconnect any existing connection first
        if (mqtt->connected()) {
            LOG_DEBUG("Disconnecting existing MQTT connection");
            mqtt->disconnect();
            delay(1000); // Give time for clean disconnect
        }
        
        unsigned long connectStart = millis();
        bool connected = mqtt->connect(MQTT_BROKER, 1883, clientName);
        
        if (connected) {
            // Wait for connection to be established with timeout
            unsigned long waitStart = millis();
            while (!mqtt->connected() && (millis() - waitStart < MQTT_CONNECTION_TIMEOUT)) {
                delay(100);
                mqtt->loop(); // Process any pending MQTT messages
            }
            
            if (mqtt->connected()) {
                unsigned long connectTime = millis() - connectStart;
                LOG_INFO("MQTT connected successfully in %lu ms (attempt %d)", connectTime, attempt);
                return true;
            } else {
                LOG_WARN("MQTT connection timed out after %lu ms (attempt %d)", MQTT_CONNECTION_TIMEOUT, attempt);
            }
        } else {
            LOG_WARN("MQTT connection failed immediately (attempt %d)", attempt);
        }
        
        // If not the last attempt, wait before retrying
        if (attempt < MAX_MQTT_RETRIES) {
            unsigned long retryDelay = MQTT_RETRY_DELAY * attempt; // Exponential backoff
            LOG_INFO("Retrying MQTT connection in %lu ms...", retryDelay);
            delay(retryDelay);
        }
    }
    
    LOG_ERROR_CODE(ERR_NETWORK_MQTT_CONNECTION_FAILED, "Failed to connect to MQTT broker after %d attempts", MAX_MQTT_RETRIES);
    return false;
}

bool NetworkManager::isMQTTConnected() {
    return mqtt->connected();
}

bool NetworkManager::ensureConnected() {
    // Check WiFi connection first
    if (!isWifiConnected()) {
        LOG_WARN("WiFi disconnected, attempting to reconnect...");
        if (!connectToWifi(false)) {
            LOG_ERROR("Failed to restore WiFi connection");
            return false;
        }
        LOG_INFO("WiFi connection restored");
    }
    
    // Check MQTT connection
    if (!isMQTTConnected()) {
        LOG_WARN("MQTT disconnected, attempting to reconnect...");
        if (!connectToMQTT()) {
            LOG_ERROR("Failed to restore MQTT connection");
            return false;
        }
        LOG_INFO("MQTT connection restored");
    }
    
    return true;
}

void NetworkManager::setupMQTT(bool resume) {
    LOG_INFO("Setting up MQTT connections and subscriptions");
    
    // Ensure WiFi is connected first
    if (!connectToWifi(true)) {
        LOG_ERROR_CODE(ERR_NETWORK_WIFI_CONNECTION_FAILED, "Cannot setup MQTT without WiFi connection");
        return;
    }
    
    // Connect to MQTT with retries
    if (!connectToMQTT()) {
        LOG_ERROR_CODE(ERR_NETWORK_MQTT_CONNECTION_FAILED, "Cannot setup MQTT subscriptions without broker connection");
        return;
    }
    
    // Give MQTT connection a moment to stabilize
    delay(500);
    
    // Setup command subscription
    LOG_DEBUG("Setting up command subscription: %s", MQTT_RECEIVE_COMMANDS_TOPIC);
    mqtt->subscribe(MQTT_RECEIVE_COMMANDS_TOPIC, [this](const char* topic, Stream& stream) {
        LOG_DEBUG("Received command on topic: %s", topic);
        if (onCommandReceived) {
            onCommandReceived(stream);
        }
    });
    
    // Setup raw model reception
    LOG_DEBUG("Setting up raw model reception: %s", MQTT_RAW_RECEIVE_TOPIC);
    mqtt->subscribe(MQTT_RAW_RECEIVE_TOPIC, [this](const char* topic, Stream& stream) {
        LOG_DEBUG("Received raw model on topic: %s", topic);
        if (onRawModelReceived) {
            onRawModelReceived(stream);
        }
    });
    
    // Setup JSON model reception
    LOG_DEBUG("Setting up JSON model reception: %s", MQTT_RECEIVE_TOPIC);
    mqtt->subscribe(MQTT_RECEIVE_TOPIC, [this](const char* topic, Stream& stream) {
        LOG_DEBUG("Received JSON model on topic: %s", topic);
        if (onModelReceived) {
            onModelReceived(stream);
        }
    });
    
    if (resume) {
        setupResume();
    }
    
    LOG_INFO("MQTT setup completed successfully");
}

void NetworkManager::setupResume() {
    // Setup JSON resume
    mqtt->subscribe(MQTT_RESUME_TOPIC, [this](const char* topic, Stream& stream) {
        if (onResumeReceived) {
            onResumeReceived(stream);
        }
    });
    
    // Setup raw resume with client-specific topic
    String topic = String(MQTT_RAW_RESUME_TOPIC);
    topic.concat("/");
    topic.concat(clientName);
    
    mqtt->subscribe(topic, [this](const char* topic, Stream& stream) {
        if (onRawResumeReceived) {
            onRawResumeReceived(stream);
        }
    });
}

void NetworkManager::processMessages() {
    if (!sendingMessage) {
        // Check connection health before processing messages
        if (!ensureConnected()) {
            LOG_WARN("Connection lost during message processing");
            return;
        }
        
        mqtt->loop();
    }
}

const char* NetworkManager::modelStateToString(ModelState state) {
    switch (state) {
        case ModelState_IDLE:
            return "idle";
        case ModelState_READY_TO_TRAIN:
            return "ready_to_train";
        case ModelState_MODEL_BUSY:
            return "model_busy";
        case ModelState_WAITING_DOWNLOAD:
            return "waiting_download";
        case ModelState_DONE_TRAINING:
            return "done_training";
        default:
            return "unknown";
    }
}

void NetworkManager::sendMessageToNetwork(FederateCommand command, int currentRound, ModelState newModelState) {
    if (!ensureConnected()) {
        LOG_ERROR_CODE(ERR_NETWORK_CONNECTION_FAILED, "Failed to ensure network connection");
        return;
    }
    
    if (sendingMessage) {
        LOG_WARN("Already sending message, skipping command");
        return;
    }
    sendingMessage = true;
    
    LOG_INFO("Sending command to network: %d", static_cast<int>(command));
    
    JsonDocument doc;
    
    switch (command) {
    case FederateCommand_JOIN:
        doc["command"] = "join";
        doc["client"] = clientName;
        break;
        
    case FederateCommand_RESUME:
        LOG_INFO("Sending resume training command for round %d", currentRound);
        doc["command"] = "resume";
        doc["client"] = clientName;
        doc["round"] = currentRound;
        break;
        
    case FederateCommand_ALIVE:
        doc["command"] = "alive";
        doc["client"] = clientName;
        doc["round"] = currentRound;
        doc["newModelState"] = modelStateToString(newModelState);
        break;
        
    default:
        sendingMessage = false;
        return;
    }
    
    auto publish = mqtt->begin_publish(MQTT_SEND_COMMANDS_TOPIC, measureJson(doc));
    serializeJson(doc, publish);
    publish.send();
    
    sendingMessage = false;
}

void NetworkManager::sendModelToNetwork(NeuralNetwork& NN, multiClassClassifierMetrics& metrics,
                                       unsigned long datasetSize, unsigned long previousTransmit,
                                       unsigned long previousConstruct, const FixedMemoryUsage& fixedMemoryUsage,
                                       const RoundMemoryUsage& roundMemoryUsage) {
    // Ensure only one message is sent at a time
    while (sendingMessage) {
        delay(10);
    }
    sendingMessage = true;
    
    if (!ensureConnected()) {
        LOG_ERROR_CODE(ERR_NETWORK_CONNECTION_FAILED, "Failed to ensure connection for model transmission");
        sendingMessage = false;
        return;
    }
    
    LOG_INFO("Sending trained model to network with metrics");
    
    JsonDocument doc;
    
#if defined(USE_64_BIT_DOUBLE)
    doc["precision"] = "double";
#else
    doc["precision"] = "float";
#endif
    
    doc["client"] = clientName;
    doc["metrics"] = JsonObject();
    doc["metrics"]["accuracy"] = metrics.accuracy();
    doc["metrics"]["precision"] = metrics.precision();
    doc["metrics"]["recall"] = metrics.recall();
    doc["metrics"]["f1Score"] = metrics.f1Score();
    doc["metrics"]["meanSqrdError"] = metrics.meanSqrdError;
    doc["metrics"]["numberOfClasses"] = metrics.numberOfClasses;
    doc["metrics"]["truePositives"] = JsonArray();
    doc["metrics"]["falsePositives"] = JsonArray();
    doc["metrics"]["trueNegatives"] = JsonArray();
    doc["metrics"]["falseNegatives"] = JsonArray();
    
    for (int i = 0; i < metrics.numberOfClasses; i++) {
        doc["metrics"]["truePositives"].add(metrics.metrics[i].truePositives);
        doc["metrics"]["falsePositives"].add(metrics.metrics[i].falsePositives);
        doc["metrics"]["trueNegatives"].add(metrics.metrics[i].trueNegatives);
        doc["metrics"]["falseNegatives"].add(metrics.metrics[i].falseNegatives);
    }
    
    doc["model"] = JsonArray();
    for (unsigned int n = 0; n < NN.numberOflayers; n++) {
        doc["model"].add(NN.layers[n]._numberOfInputs);
    }
    doc["model"].add(NN.layers[NN.numberOflayers - 1]._numberOfOutputs);
    doc["epochs"] = metrics.epochs;
    doc["datasetSize"] = datasetSize;
    doc["timings"] = JsonObject();
    doc["timings"]["previousTransmit"] = previousTransmit;
    doc["timings"]["previousConstruct"] = previousConstruct;
    doc["timings"]["training"] = metrics.trainingTime;
    doc["timings"]["parsing"] = metrics.parsingTime;
    
    doc["memory"] = JsonObject();
    doc["memory"]["fixed"] = JsonObject();
    doc["memory"]["fixed"]["onBoot"] = fixedMemoryUsage.onBoot;
    doc["memory"]["fixed"]["loadConfig"] = fixedMemoryUsage.loadConfig;
    doc["memory"]["fixed"]["loadAndTrainModel"] = fixedMemoryUsage.loadAndTrainModel;
    doc["memory"]["fixed"]["connectionMade"] = fixedMemoryUsage.connectionMade;
    doc["memory"]["fixed"]["afterFullSetup"] = fixedMemoryUsage.afterFullSetup;
    doc["memory"]["fixed"]["minFreeHeapAfterSetup"] = fixedMemoryUsage.minFreeHeapAfterSetup;
    
    doc["memory"]["round"] = JsonObject();
    doc["memory"]["round"]["messageReceived"] = roundMemoryUsage.messageReceived;
    doc["memory"]["round"]["beforeTrain"] = roundMemoryUsage.beforeTrain;
    doc["memory"]["round"]["afterTrain"] = roundMemoryUsage.afterTrain;
    doc["memory"]["round"]["beforeSend"] = roundMemoryUsage.beforeSend;
    doc["memory"]["round"]["minimumFree"] = roundMemoryUsage.minimumFree;
    
    // Add biases and weights arrays
    doc["biases"] = JsonArray();
    doc["weights"] = JsonArray();
    
    for (unsigned int i = 0; i < NN.numberOflayers; i++) {
#if !defined(NO_BIAS)
#if defined(MULTIPLE_BIASES_PER_LAYER)
        // Multiple biases per layer - one bias per output neuron
        for (unsigned int j = 0; j < NN.layers[i]._numberOfOutputs; j++) {
            doc["biases"].add(NN.layers[i].bias[j]);
        }
#else
        // Single bias per layer - one bias shared by all output neurons
        // Add the bias value only once per layer, not once per output neuron
        doc["biases"].add(*NN.layers[i].bias);
#endif
#endif
    }
    
    for (unsigned int i = 0; i < NN.numberOflayers; i++) {
        for (unsigned int j = 0; j < NN.layers[i]._numberOfOutputs; j++) {
            for (unsigned int k = 0; k < NN.layers[i]._numberOfInputs; k++) {
#if defined(REDUCE_RAM_WEIGHTS_LVL2)
                // Global 1D weights array - access via global NN.weights with calculated index
                unsigned int globalIndex = 0;
                // Calculate global index by summing weights from previous layers
                for (unsigned int prevLayer = 0; prevLayer < i; prevLayer++) {
                    globalIndex += NN.layers[prevLayer]._numberOfInputs * NN.layers[prevLayer]._numberOfOutputs;
                }
                globalIndex += j * NN.layers[i]._numberOfInputs + k;
                doc["weights"].add(NN.weights[globalIndex]);
#elif !defined(REDUCE_RAM_WEIGHTS_COMMON)
                // Standard 2D weights array access
                doc["weights"].add(NN.layers[i].weights[j][k]);
#else
                // REDUCE_RAM_WEIGHTS_COMMON: 1D weights array per layer
                unsigned int index = j * NN.layers[i]._numberOfInputs + k;
                doc["weights"].add(NN.layers[i].weights[index]);
#endif
            }
        }
    }
    
    // Save model to temporary file for binary transmission
    File tempModelFile = LittleFS.open(TEMPORARY_NEW_MODEL_PATH, "w");
    if (tempModelFile) {
        bool modelSaved = NN.save(tempModelFile);
        tempModelFile.close();
        
        if (modelSaved) {
            LOG_DEBUG("Model saved to temporary file for binary transmission");
            
            // Send binary model data
            sendBinaryModelToNetwork(NN);
        } else {
            LOG_ERROR_CODE(ERR_FS_WRITE_FAILED, "Failed to save model to temporary file");
        }
    } else {
        LOG_ERROR_CODE(ERR_FS_OPEN_FAILED, "Failed to open temporary file for writing: %s", TEMPORARY_NEW_MODEL_PATH);
    }
    
    // Send JSON metadata via MQTT
    auto publish = mqtt->begin_publish(MQTT_PUBLISH_TOPIC, measureJson(doc));
    serializeJson(doc, publish);
    publish.send();
    
    sendingMessage = false;
}

void NetworkManager::sendBinaryModelToNetwork(NeuralNetwork& NN) {
    LOG_INFO("Sending binary model to network");
    
    if (!LittleFS.exists(TEMPORARY_NEW_MODEL_PATH)) {
        LOG_ERROR_CODE(ERR_FS_FILE_NOT_FOUND, "Temporary model file not found: %s", TEMPORARY_NEW_MODEL_PATH);
        return;
    }
    
    File modelFile = LittleFS.open(TEMPORARY_NEW_MODEL_PATH, "r");
    if (!modelFile) {
        LOG_ERROR_CODE(ERR_FS_OPEN_FAILED, "Failed to open temporary model file for reading: %s", TEMPORARY_NEW_MODEL_PATH);
        return;
    }
    
    size_t fileSize = modelFile.size();
    LOG_INFO("Binary model file size: %d bytes", fileSize);
    
    // Create topic for binary transmission
    String topic = String(MQTT_RAW_PUBLISH_TOPIC);
    topic.concat("/");
    topic.concat(clientName);
    
    LOG_DEBUG("Binary transmission topic: %s", topic.c_str());
    
    // Begin binary publish
    auto publish = mqtt->begin_publish(topic.c_str(), fileSize);
    
    // Send file in chunks
    const size_t BUFFER_SIZE = 1024;
    uint8_t buffer[BUFFER_SIZE];
    size_t totalBytesSent = 0;
    
    while (modelFile.available()) {
        size_t bytesRead = modelFile.readBytes((char*)buffer, BUFFER_SIZE);
        if (bytesRead > 0) {
            publish.write(buffer, bytesRead);
            totalBytesSent += bytesRead;
        }
    }
    
    publish.send();
    modelFile.close();
    
    LOG_INFO("Binary model sent successfully (%d bytes)", totalBytesSent);
}

void NetworkManager::checkConnectionHealth() {
    LOG_INFO("=== Connection Health Check ===");
    
    // WiFi status check
    LOG_INFO("WiFi Status: %s", isWifiConnected() ? "CONNECTED" : "DISCONNECTED");
    if (isWifiConnected()) {
        LOG_INFO("WiFi IP: %s", WiFi.localIP().toString().c_str());
        LOG_INFO("WiFi RSSI: %d dBm", WiFi.RSSI());
        LOG_INFO("WiFi Gateway: %s", WiFi.gatewayIP().toString().c_str());
    } else {
        LOG_WARN("WiFi disconnected - status code: %d", WiFi.status());
        switch(WiFi.status()) {
            case WL_NO_SSID_AVAIL:
                LOG_WARN("WiFi Issue: SSID '%s' not available", WIFI_SSID);
                break;
            case WL_CONNECT_FAILED:
                LOG_WARN("WiFi Issue: Connection failed - check password");
                break;
            case WL_CONNECTION_LOST:
                LOG_WARN("WiFi Issue: Connection lost");
                break;
            case WL_DISCONNECTED:
                LOG_WARN("WiFi Issue: Disconnected");
                break;
            default:
                LOG_WARN("WiFi Issue: Unknown status %d", WiFi.status());
                break;
        }
    }
    
    // MQTT status check
    LOG_INFO("MQTT Status: %s", isMQTTConnected() ? "CONNECTED" : "DISCONNECTED");
    if (!isMQTTConnected()) {
        LOG_WARN("MQTT disconnected - broker: %s:1883", MQTT_BROKER);
        LOG_WARN("Client ID: %s", clientName);
    }
    
    // Memory status
    LOG_INFO("Free Heap: %d bytes", ESP.getFreeHeap());
    LOG_INFO("Min Free Heap: %d bytes", ESP.getMinFreeHeap());
    
    LOG_INFO("=== End Health Check ===");
}
