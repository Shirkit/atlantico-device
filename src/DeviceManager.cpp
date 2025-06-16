#include "DeviceManager.h"
#include <ArduinoJson.h>
#include <LittleFS.h>

// Global variables for memory tracking
multi_heap_info_t info;
unsigned long previousMillis = 0;

DeviceManager::DeviceManager() {
    deviceConfig = nullptr;
    clientName = nullptr;
    modelManager = new ModelManager();
    networkManager = nullptr;
    
    newModelState = ModelState_IDLE;
    federateState = FederateState_NONE;
    currentRound = -1;
    waitingForMe = false;
    unsubscribeFromResume = false;
    
    fixedMemoryUsage = {0, 0, 0, 0, 0, 0};
    roundMemoryUsage = {0, 0, 0, 0, 0};
    previousTransmit = 0;
    previousConstruct = 0;
}

DeviceManager::~DeviceManager() {
    if (deviceConfig) {
        delete deviceConfig;
    }
    if (clientName) {
        delete[] clientName;
    }
    if (modelManager) {
        delete modelManager;
    }
    if (networkManager) {
        delete networkManager;
    }
}

void DeviceManager::bootUp(bool initBaseModel) {
    LOG_INFO("Starting device boot process...");
    
    if (!LittleFS.begin(false)) {
        LOG_ERROR_CODE(ERR_FS_MOUNT_FAILED, "LittleFS Mount Failed");
        return;
    }
    LOG_INFO("LittleFS mounted successfully");
    
    auto result = loadDeviceDefinitions();
    if (result.isError()) {
        LOG_ERROR_CODE(result.code, "Failed to load device definitions: %s", result.message);
        return;
    }
    LOG_INFO("Device name: %s", clientName);
    
    LOG_INFO("Loading device configuration...");
    
    auto configResult = loadDeviceConfig();
    bool resumeTraining = false;
    
    if (configResult.isSuccess()) {
        LOG_INFO("Device configuration loaded successfully");
        resumeTraining = (deviceConfig->currentFederateState == FederateState_TRAINING || 
                         deviceConfig->currentFederateState == FederateState_STARTING);
        currentRound = deviceConfig->currentRound;
        newModelState = deviceConfig->newModelState;
        federateState = deviceConfig->currentFederateState;
    } else {
        LOG_WARN("Could not load device configuration: %s", configResult.message);
        // Continue with default values
    }
    
    // printMemory();
    fixedMemoryUsage.loadConfig = info.total_free_bytes;
    
    if (initBaseModel) {
        modelManager->initializeModels(true);
    }
    
    if (deviceConfig != nullptr) {
        // Load federated model configuration if available
        if (deviceConfig->loadedFederateModelConfig != nullptr) {
            modelManager->setFederateModelConfig(deviceConfig->loadedFederateModelConfig);
            modelManager->setupFederatedModel();
        }
    }
    
    // printMemory();
    fixedMemoryUsage.loadAndTrainModel = info.total_free_bytes;
    
    // Initialize network manager
    networkManager = new NetworkManager(clientName);
    
    // Set up callbacks
    networkManager->onModelReceived = [this](Stream& stream) { onModelReceived(stream); };
    networkManager->onRawModelReceived = [this](Stream& stream) { onRawModelReceived(stream); };
    networkManager->onCommandReceived = [this](Stream& stream) { onCommandReceived(stream); };
    networkManager->onResumeReceived = [this](Stream& stream) { onResumeReceived(stream); };
    networkManager->onRawResumeReceived = [this](Stream& stream) { onRawResumeReceived(stream); };
    
    networkManager->setupMQTT(resumeTraining);
    networkManager->ensureConnected();
    
    // Enable remote logging now that NetworkManager is ready
    #if USE_ADVANCED_LOGGER
    Logger::enableRemoteLogging(networkManager);
    LOG_INFO("Remote logging enabled for critical errors");
    #endif
    
    if (resumeTraining) {
        networkManager->sendMessageToNetwork(FederateCommand_RESUME, currentRound, newModelState);
    }
    
    // printMemory();
    fixedMemoryUsage.connectionMade = info.total_free_bytes;
    
    LOG_INFO("Device boot completed successfully");
}

ErrorResult DeviceManager::loadDeviceDefinitions() {
    File file = LittleFS.open(DEVICE_DEFINITION_PATH, "r");
    RETURN_ERROR_IF(!file, ERR_FS_FILE_NOT_FOUND, "Failed to open device definition file: %s", DEVICE_DEFINITION_PATH);
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    
    RETURN_ERROR_IF(error, ERR_FS_JSON_PARSE_FAILED, "Failed to parse device definition JSON: %s", error.c_str());
    
    const char* name = doc["client"];
    RETURN_ERROR_IF(!name, ERR_CONFIG_MISSING_REQUIRED, "Missing 'client' field in device definition");
    
    clientName = new char[strlen(name) + 1];
    strcpy(clientName, name);
    
    LOG_DEBUG("Device name loaded: %s", clientName);
    return ErrorResult(ERR_SUCCESS);
}

ErrorResult DeviceManager::loadDeviceConfig() {
    File file = LittleFS.open(CONFIGURATION_PATH, "r");
    if (!file) {
        LOG_WARN("No configuration file found at %s", CONFIGURATION_PATH);
        return ErrorResult(ERR_FS_FILE_NOT_FOUND, "Configuration file not found");
    }
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    
    RETURN_ERROR_IF(error, ERR_FS_JSON_PARSE_FAILED, "Failed to parse configuration JSON: %s", error.c_str());
    
    deviceConfig = new DeviceConfig();
    deviceConfig->currentRound = doc["currentRound"].as<int>();
    deviceConfig->currentFederateState = static_cast<FederateState>(doc["currentFederateState"].as<int>());
    deviceConfig->newModelState = static_cast<ModelState>(doc["newModelState"].as<int>());
    
    LOG_INFO("Device configuration loaded successfully");
    return ErrorResult(ERR_SUCCESS);
}

ErrorResult DeviceManager::saveDeviceConfig() {
    if (!deviceConfig) {
        deviceConfig = new DeviceConfig();
    }
    
    deviceConfig->currentRound = currentRound;
    deviceConfig->currentFederateState = federateState;
    deviceConfig->newModelState = newModelState;
    
    File file = LittleFS.open(CONFIGURATION_PATH, "w");
    RETURN_ERROR_IF(!file, ERR_FS_FILE_WRITE_FAILED, "Failed to open config file for writing: %s", CONFIGURATION_PATH);
    
    JsonDocument doc;
    doc["currentRound"] = deviceConfig->currentRound;
    doc["currentFederateState"] = static_cast<int>(deviceConfig->currentFederateState);
    doc["newModelState"] = static_cast<int>(deviceConfig->newModelState);
    
    size_t bytesWritten = serializeJson(doc, file);
    file.close();
    
    RETURN_ERROR_IF(bytesWritten == 0, ERR_FS_FILE_WRITE_FAILED, "Failed to write configuration data");
    
    LOG_INFO("Device configuration saved successfully (%zu bytes)", bytesWritten);
    return ErrorResult(ERR_SUCCESS);
}

void DeviceManager::printMemory() {
    // Always capture memory info for tracking, regardless of DEBUG setting
    heap_caps_get_info(&info, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    
    LOG_DEBUG("Heap info: %d bytes free", info.total_free_bytes);
    LOG_DEBUG("Heap info: %d bytes largest free block", info.largest_free_block);
    LOG_DEBUG("Heap info: %d bytes minimum free ever", info.minimum_free_bytes);
}

void DeviceManager::printTiming(bool doReset) {
    if (doReset || previousMillis == 0) {
        previousMillis = millis();
        LOG_DEBUG("Timing reset at %lu ms", previousMillis);
    } else {
        unsigned long currentMillis = millis();
        float timeElapsed = ((float)currentMillis - previousMillis) / 1000.0;
        LOG_DEBUG("Time elapsed: %.1f seconds (%lu ms)", timeElapsed, currentMillis - previousMillis);
        previousMillis = 0;
    }
}

void DeviceManager::captureOnBootMemory() {
    // Ensure memory info is captured
    heap_caps_get_info(&info, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    fixedMemoryUsage.onBoot = info.total_free_bytes;
    
    // Also print for debugging if enabled
    // printMemory();
}

void DeviceManager::captureAfterFullSetupMemory() {
    // Ensure memory info is captured
    heap_caps_get_info(&info, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    fixedMemoryUsage.afterFullSetup = info.total_free_bytes;
    fixedMemoryUsage.minFreeHeapAfterSetup = info.minimum_free_bytes;
    
    // Also print for debugging if enabled
    // printMemory();
}

void DeviceManager::processModel() {
    // Main model processing logic
    switch (newModelState) {
        case ModelState_READY_TO_TRAIN:
            if (modelManager->getNewModel()) {
                LOG_INFO("Training new model with federated configuration");
                heap_caps_get_info(&info, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
                roundMemoryUsage.beforeTrain = info.total_free_bytes;
                // printMemory();
                
                auto metrics = modelManager->trainModelFromOriginalDataset(
                    *modelManager->getNewModel(), 
                    *modelManager->getFederateModelConfig(), 
                    X_TRAIN_PATH, 
                    Y_TRAIN_PATH
                );
                modelManager->setNewModelMetrics(metrics);
                
                heap_caps_get_info(&info, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
                roundMemoryUsage.afterTrain = info.total_free_bytes;
                // printMemory();
                
                newModelState = ModelState_DONE_TRAINING;
                auto saveResult = saveDeviceConfig();
                if (saveResult.isError()) {
                    LOG_ERROR_CODE(saveResult.code, "Failed to save device config: %s", saveResult.message);
                }
            }
            break;
            
        case ModelState_DONE_TRAINING:
            if (modelManager->getNewModel() && modelManager->getNewModelMetrics()) {
                LOG_INFO("Sending trained model to network");
                heap_caps_get_info(&info, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
                roundMemoryUsage.beforeSend = info.total_free_bytes;
                roundMemoryUsage.minimumFree = info.minimum_free_bytes;
                // printMemory();
                
                networkManager->sendModelToNetwork(
                    *modelManager->getNewModel(), 
                    *modelManager->getNewModelMetrics(),
                    modelManager->getDatasetSize(),
                    previousTransmit,
                    previousConstruct,
                    fixedMemoryUsage,
                    roundMemoryUsage
                );
                newModelState = ModelState_WAITING_DOWNLOAD;
                auto saveResult = saveDeviceConfig();
                if (saveResult.isError()) {
                    LOG_ERROR_CODE(saveResult.code, "Failed to save device config: %s", saveResult.message);
                }
            }
            break;
            
        default:
            // No action needed for other states
            break;
    }
}

void DeviceManager::processMessages() {
    if (networkManager) {
        networkManager->processMessages();
    }
}

// Network event callbacks
void DeviceManager::onModelReceived(Stream& stream) {
    LOG_INFO("Received JSON model from network");
    heap_caps_get_info(&info, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    roundMemoryUsage.messageReceived = info.total_free_bytes;
    // printMemory();
    
    model* receivedModel = modelManager->transformDataToModel(stream);
    if (receivedModel) {
        // Process the received model
        LOG_INFO("Model received for round %d", receivedModel->round);
        delete receivedModel;
    }
}

void DeviceManager::onRawModelReceived(Stream& stream) {
    LOG_INFO("Received raw model from network");
    heap_caps_get_info(&info, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    roundMemoryUsage.messageReceived = info.total_free_bytes;
    // printMemory();
    
    // Save raw model data to temporary file
    File modelFile = LittleFS.open(TEMPORARY_NEW_MODEL_PATH, "w");
    if (!modelFile) {
        LOG_ERROR_CODE(ERR_FS_OPEN_FAILED, "Failed to open temporary model file for writing: %s", TEMPORARY_NEW_MODEL_PATH);
        return;
    }
    
    // Write the entire stream to file
    size_t bytesWritten = 0;
    while (stream.available()) {
        uint8_t byte = stream.read();
        modelFile.write(byte);
        bytesWritten++;
    }
    modelFile.close();
    
    LOG_INFO("Raw model saved to temporary file (%d bytes): %s", bytesWritten, TEMPORARY_NEW_MODEL_PATH);
    
    // Load the model using NeuralNetwork library
    auto newModel = modelManager->loadModelFromFlash(TEMPORARY_NEW_MODEL_PATH);
    if (newModel) {
        // Replace the current new model
        if (modelManager->getNewModel()) {
            delete modelManager->getNewModel();
        }
        modelManager->setNewModel(newModel);
        newModelState = ModelState_READY_TO_TRAIN;
        auto saveResult = saveDeviceConfig();
        if (saveResult.isError()) {
            LOG_ERROR_CODE(saveResult.code, "Failed to save device config: %s", saveResult.message);
        }
        LOG_INFO("Raw model loaded successfully and ready for training");
    } else {
        LOG_ERROR("Failed to load raw model from temporary file");
    }
}

void DeviceManager::onCommandReceived(Stream& stream) {
    LOG_INFO("Received command from network");
    heap_caps_get_info(&info, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    roundMemoryUsage.messageReceived = info.total_free_bytes;
    // printMemory();
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, stream);
    
    if (error) {
        LOG_ERROR("Failed to parse command JSON: %s", error.c_str());
        return;
    }
    
    const char* command = doc["command"];
    LOG_INFO("Received command: %s", command);
    
    if (strcmp(command, "federate_join") == 0) {
        if (federateState != FederateState_NONE && federateState != FederateState_SUBSCRIBED) {
            LOG_ERROR("Device already in a federated state, cannot join again");
            return;
        }
        federateState = FederateState_SUBSCRIBED;
        networkManager->sendMessageToNetwork(FederateCommand_JOIN);
        auto saveResult = saveDeviceConfig();
        if (saveResult.isError()) {
            LOG_ERROR_CODE(saveResult.code, "Failed to save device config: %s", saveResult.message);
        }
    }
    else if (strcmp(command, "federate_start") == 0) {
        if (federateState == FederateState_SUBSCRIBED) {
            LOG_INFO("Processing federate_start command");
            if (doc["config"].is<JsonObject>()) {
                JsonObject config = doc["config"];
                
                LOG_DEBUG("Config layers size: %d", config["layers"].size());
                LOG_DEBUG("Config actvFunctions size: %d", config["actvFunctions"].size());
                
                // Extract layers configuration
                unsigned int* federateLayers = new unsigned int[config["layers"].size()];
                for (int i = 0; i < config["layers"].size(); i++) {
                    federateLayers[i] = config["layers"][i].as<unsigned int>();
                    LOG_DEBUG("Layer %d: %u neurons", i, federateLayers[i]);
                }
                
                // Extract activation functions
                byte* federateActvFunctions = new byte[config["actvFunctions"].size()];
                for (int i = 0; i < config["actvFunctions"].size(); i++) {
                    federateActvFunctions[i] = config["actvFunctions"][i].as<byte>();
                    LOG_DEBUG("Activation function %d: %u", i, federateActvFunctions[i]);
                }
                
                // Create federate model configuration
                ModelConfig* federateModelConfig = new ModelConfig(
                    federateLayers, 
                    config["layers"].size(), 
                    federateActvFunctions
                );
                
                LOG_INFO("Created federate model configuration");
                
                // Set optional parameters
                if (doc["randomSeed"].is<unsigned long>()) {
                    federateModelConfig->randomSeed = doc["randomSeed"].as<unsigned long>();
                    randomSeed(federateModelConfig->randomSeed);
                    LOG_DEBUG("Set random seed: %lu", federateModelConfig->randomSeed);
                }
                if (config["epochs"].is<unsigned int>()) {
                    federateModelConfig->epochs = config["epochs"].as<unsigned int>();
                    LOG_DEBUG("Set epochs: %u", federateModelConfig->epochs);
                }
                if (config["learningRateOfWeights"].is<IDFLOAT>()) {
                    federateModelConfig->learningRateOfWeights = config["learningRateOfWeights"].as<IDFLOAT>();
                    LOG_DEBUG("Set learning rate weights: %f", federateModelConfig->learningRateOfWeights);
                }
                if (config["learningRateOfBiases"].is<IDFLOAT>()) {
                    federateModelConfig->learningRateOfBiases = config["learningRateOfBiases"].as<IDFLOAT>();
                    LOG_DEBUG("Set learning rate biases: %f", federateModelConfig->learningRateOfBiases);
                }
                
                // Set up the federated model
                modelManager->setFederateModelConfig(federateModelConfig);
                modelManager->setupFederatedModel();
                
                // Verify the model was created successfully
                if (modelManager->getNewModel()) {
                    // Update state only if successful
                    federateState = FederateState_TRAINING;
                    currentRound = 0;
                    newModelState = ModelState_READY_TO_TRAIN;
                    
                    auto saveResult = saveDeviceConfig();
                    if (saveResult.isError()) {
                        LOG_ERROR_CODE(saveResult.code, "Failed to save device config: %s", saveResult.message);
                    }
                    LOG_INFO("Federate training started successfully");
                } else {
                    LOG_ERROR("Failed to create federated model");
                }
            } else {
                LOG_ERROR("No config object in federate_start command");
            }
        } else {
            LOG_ERROR("Device not subscribed to federation");
        }
    }
    else if (strcmp(command, "federate_end") == 0) {
        federateState = FederateState_DONE;
        auto saveResult = saveDeviceConfig();
        if (saveResult.isError()) {
            LOG_ERROR_CODE(saveResult.code, "Failed to save device config: %s", saveResult.message);
        }
    }
    else if (strcmp(command, "federate_unsubscribe") == 0) {
        federateState = FederateState_NONE;
        currentRound = -1;
        newModelState = ModelState_IDLE;
        auto saveResult = saveDeviceConfig();
        if (saveResult.isError()) {
            LOG_ERROR_CODE(saveResult.code, "Failed to save device config: %s", saveResult.message);
        }
    }
    else if (strcmp(command, "federate_alive") == 0) {
        // Respond to alive check from server
        networkManager->sendMessageToNetwork(FederateCommand_ALIVE);
        LOG_INFO("Responded to federate_alive command");
    }
}

void DeviceManager::onResumeReceived(Stream& stream) {
    LOG_INFO("Received JSON resume data");
    heap_caps_get_info(&info, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    roundMemoryUsage.messageReceived = info.total_free_bytes;
    // printMemory();
    
    model* resumeModel = modelManager->transformDataToModel(stream);
    if (resumeModel) {
        LOG_INFO("Resume model received for round %d", resumeModel->round);
        // Apply the resume model
        delete resumeModel;
    } else {
        LOG_ERROR("Failed to parse resume model from JSON data");
    }
}

void DeviceManager::onRawResumeReceived(Stream& stream) {
    LOG_INFO("Received raw resume data");
    heap_caps_get_info(&info, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    roundMemoryUsage.messageReceived = info.total_free_bytes;
    // printMemory();
    
    // Save raw resume model to file
    File modelFile = LittleFS.open(NEW_MODEL_PATH, "w");
    if (!modelFile) {
        LOG_ERROR_CODE(ERR_FS_OPEN_FAILED, "Failed to open new model file for writing: %s", NEW_MODEL_PATH);
        return;
    }
    
    // Write the entire stream to file
    size_t bytesWritten = 0;
    while (stream.available()) {
        uint8_t byte = stream.read();
        modelFile.write(byte);
        bytesWritten++;
    }
    modelFile.close();
    
    LOG_INFO("Raw resume model saved (%d bytes) to %s", bytesWritten, NEW_MODEL_PATH);
    
    // Load the resume model using NeuralNetwork library
    auto resumeModel = modelManager->loadModelFromFlash(NEW_MODEL_PATH);
    if (resumeModel) {
        // Replace the current new model with the resume model
        if (modelManager->getNewModel()) {
            delete modelManager->getNewModel();
        }
        modelManager->setNewModel(resumeModel);
        newModelState = ModelState_READY_TO_TRAIN;
        auto saveResult = saveDeviceConfig();
        if (saveResult.isError()) {
            LOG_ERROR_CODE(saveResult.code, "Failed to save device config: %s", saveResult.message);
        }
        LOG_INFO("Raw resume model loaded successfully and ready for training");
    } else {
        LOG_ERROR("Failed to load raw resume model");
    }
}
