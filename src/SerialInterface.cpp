#include "SerialInterface.h"

SerialInterface::SerialInterface(DeviceManager* deviceManager) {
    this->deviceManager = deviceManager;
}

void SerialInterface::printInstructions() {
    LOG_INFO("Displaying user menu options");
    Serial.println("Choose an option to continue:");
    Serial.println("=== Current Model ===");
    Serial.println("1. Print Model");
    Serial.println("2. Train Model");
    Serial.println("3. Save Model");
    Serial.println("4. Load Model");
    Serial.println("5. Send Model to Network");
    Serial.println("6. Print Model Metrics");
    Serial.println("7. Read test Data and Predict from Current Model");
    Serial.println("9. Delete Model");
    Serial.println("");
    Serial.println("=== New Model ===");
    Serial.println("11. Print New Model");
    Serial.println("12. Train New Model");
    Serial.println("13. Save New Model");
    Serial.println("14. Load New Model");
    Serial.println("15. Send New Model to Network");
    Serial.println("16. Print New Model Metrics");
    Serial.println("19. Delete New Model");
    Serial.println("");
    Serial.println("=== System ===");
    Serial.println("8. Print Memory usage");
    Serial.println("10. Delete Configuration");
    Serial.println("20. Reset Federated State");
    Serial.println("99. Print these Instructions");
}

void SerialInterface::processSerial() {
    if (Serial.available() != 0) {
        int option = Serial.parseInt();
        LOG_DEBUG("Processing serial command: %d", option);
        
        if (option >= 1 && option <= 9) {
            LOG_DEBUG("Routing to current model commands");
            handleCurrentModelCommands(option);
        } else if (option >= 11 && option <= 19) {
            LOG_DEBUG("Routing to new model commands");
            handleNewModelCommands(option);
        } else {
            LOG_DEBUG("Routing to system commands");
            handleSystemCommands(option);
        }
    }
}

void SerialInterface::handleCurrentModelCommands(int option) {
    LOG_DEBUG("Handling current model command: %d", option);
    auto modelManager = deviceManager->getModelManager();
    auto currentModel = modelManager->getCurrentModel();
    auto currentMetrics = modelManager->getCurrentModelMetrics();
    
    switch (option) {
    case 1:
        if (currentModel) {
            LOG_INFO("Printing current model structure");
            currentModel->print();
        } else {
            LOG_WARN("No current model loaded");
            Serial.println("No current model loaded");
        }
        break;
        
    case 2:
        if (currentModel && modelManager->getLocalModelConfig()) {
            LOG_INFO("Starting training of current model");
            Serial.println("Training current model...");
            auto metrics = modelManager->trainModelFromOriginalDataset(
                *currentModel, 
                *modelManager->getLocalModelConfig(), 
                X_TRAIN_PATH, 
                Y_TRAIN_PATH
            );
            modelManager->setCurrentModelMetrics(metrics);
            LOG_INFO("Model training completed successfully");
            Serial.println("Training complete");
        } else {
            LOG_ERROR("Cannot train: No model or config available");
            Serial.println("No model or config available for training");
        }
        break;
        
    case 3:
        if (currentModel) {
            LOG_INFO("Saving current model to flash");
            auto result = modelManager->saveModelToFlash(*currentModel, MODEL_PATH);
            if (result.isSuccess()) {
                LOG_INFO("Model saved successfully to %s", MODEL_PATH);
                Serial.println("Model saved successfully");
            } else {
                LOG_ERROR_CODE(result.errorCode, "Failed to save model: %s", result.message);
                Serial.println("Failed to save model");
            }
        } else {
            LOG_WARN("No current model to save");
            Serial.println("No current model to save");
        }
        break;
        
    case 4: {
        LOG_INFO("Loading model from flash: %s", MODEL_PATH);
        auto loadedModel = modelManager->loadModelFromFlash(MODEL_PATH);
        if (loadedModel) {
            modelManager->setCurrentModel(loadedModel);
            LOG_INFO("Model loaded successfully from flash");
            Serial.println("Model loaded successfully");
        } else {
            LOG_ERROR("Failed to load model from %s", MODEL_PATH);
            Serial.println("Failed to load model");
        }
        break;
    }
    
    case 5:
        if (currentModel && currentMetrics) {
            LOG_INFO("Sending current model to network");
            FixedMemoryUsage dummyFixed = {0, 0, 0, 0, 0, 0};
            RoundMemoryUsage dummyRound = {0, 0, 0, 0, 0};
            deviceManager->getNetworkManager()->sendModelToNetwork(
                *currentModel, 
                *currentMetrics,
                modelManager->getDatasetSize(),
                0, 0, // TODO: Get actual timing values
                dummyFixed,
                dummyRound
            );
            LOG_INFO("Model sent to network successfully");
            Serial.println("Model sent to network");
        } else {
            LOG_ERROR("Cannot send model: No model or metrics available");
            Serial.println("No model or metrics available to send");
        }
        break;
        
    case 6:
        if (currentMetrics) {
            LOG_INFO("Printing current model metrics");
            currentMetrics->print();
        } else {
            LOG_WARN("No current model metrics available");
            Serial.println("No current model metrics available");
        }
        break;
        
    case 7:
        LOG_INFO("Starting test and prediction");
        handleTestAndPrediction();
        break;
        
    case 9:
        LOG_INFO("Deleting current model from storage");
        if (LittleFS.exists(MODEL_PATH)) {
            LittleFS.remove(MODEL_PATH);
            LOG_INFO("Model file deleted: %s", MODEL_PATH);
            Serial.println("Model file deleted");
        } else {
            LOG_WARN("Model file not found: %s", MODEL_PATH);
            Serial.println("Model file not found");
        }
        break;
    }
}

void SerialInterface::handleNewModelCommands(int option) {
    LOG_DEBUG("Handling new model command: %d", option);
    auto modelManager = deviceManager->getModelManager();
    auto newModel = modelManager->getNewModel();
    auto newMetrics = modelManager->getNewModelMetrics();
    
    switch (option) {
    case 11:
        if (newModel) {
            LOG_INFO("Printing new model structure");
            newModel->print();
        } else {
            LOG_WARN("No new model loaded");
            Serial.println("No new model loaded");
        }
        break;
        
    case 12:
        if (newModel && modelManager->getFederateModelConfig()) {
            LOG_INFO("Starting training of new model");
            Serial.println("Training new model...");
            auto metrics = modelManager->trainModelFromOriginalDataset(
                *newModel, 
                *modelManager->getFederateModelConfig(), 
                X_TRAIN_PATH, 
                Y_TRAIN_PATH
            );
            modelManager->setNewModelMetrics(metrics);
            LOG_INFO("New model training completed successfully");
            Serial.println("Training complete");
        } else {
            LOG_ERROR("Cannot train new model: No model or config available");
            Serial.println("No new model or config available for training");
        }
        break;
        
    case 13:
        if (newModel) {
            LOG_INFO("Saving new model to flash");
            auto result = modelManager->saveModelToFlash(*newModel, NEW_MODEL_PATH);
            if (result.isSuccess()) {
                LOG_INFO("New model saved successfully to %s", NEW_MODEL_PATH);
                Serial.println("New model saved successfully");
            } else {
                LOG_ERROR_CODE(result.errorCode, "Failed to save new model: %s", result.message);
                Serial.println("Failed to save new model");
            }
        } else {
            LOG_WARN("No new model to save");
            Serial.println("No new model to save");
        }
        break;
        
    case 14: {
        LOG_INFO("Loading new model from flash: %s", NEW_MODEL_PATH);
        auto loadedModel = modelManager->loadModelFromFlash(NEW_MODEL_PATH);
        if (loadedModel) {
            modelManager->setNewModel(loadedModel);
            LOG_INFO("New model loaded successfully from flash");
            Serial.println("New model loaded successfully");
        } else {
            LOG_ERROR("Failed to load new model from %s", NEW_MODEL_PATH);
            Serial.println("Failed to load new model");
        }
        break;
    }
    
    case 15:
        if (newModel && newMetrics) {
            LOG_INFO("Sending new model to network");
            FixedMemoryUsage dummyFixed = {0, 0, 0, 0, 0, 0};
            RoundMemoryUsage dummyRound = {0, 0, 0, 0, 0};
            deviceManager->getNetworkManager()->sendModelToNetwork(
                *newModel, 
                *newMetrics,
                modelManager->getDatasetSize(),
                0, 0, // TODO: Get actual timing values
                dummyFixed,
                dummyRound
            );
            LOG_INFO("New model sent to network successfully");
            Serial.println("New model sent to network");
        } else {
            LOG_ERROR("Cannot send new model: No model or metrics available");
            Serial.println("No new model or metrics available to send");
        }
        break;
        
    case 16:
        if (newMetrics) {
            LOG_INFO("Printing new model metrics");
            newMetrics->print();
        } else {
            LOG_WARN("No new model metrics available");
            Serial.println("No new model metrics available");
        }
        break;
        
    case 19:
        LOG_INFO("Deleting new model from storage");
        if (LittleFS.exists(NEW_MODEL_PATH)) {
            LittleFS.remove(NEW_MODEL_PATH);
            LOG_INFO("New model file deleted: %s", NEW_MODEL_PATH);
            Serial.println("New model file deleted");
        } else {
            LOG_WARN("New model file not found: %s", NEW_MODEL_PATH);
            Serial.println("New model file not found");
        }
        break;
    }
}

void SerialInterface::handleSystemCommands(int option) {
    LOG_DEBUG("Handling system command: %d", option);
    
    switch (option) {
    case 8:
        LOG_INFO("Displaying memory usage");
        deviceManager->printMemory();
        break;
        
    case 10:
        LOG_INFO("Attempting to delete configuration file: %s", CONFIGURATION_PATH);
        if (LittleFS.exists(CONFIGURATION_PATH)) {
            bool success = LittleFS.remove(CONFIGURATION_PATH);
            if (success) {
                LOG_INFO("Configuration file deleted successfully");
                Serial.println("Configuration file deleted");
            } else {
                LOG_ERROR("Failed to delete configuration file: %s", CONFIGURATION_PATH);
                Serial.println("Failed to delete configuration file");
            }
        } else {
            LOG_WARN("Configuration file not found: %s", CONFIGURATION_PATH);
            Serial.println("Configuration file not found");
        }
        break;
        
    case 20: {
        LOG_INFO("Resetting federated learning state");
        deviceManager->setFederateState(FederateState_NONE);
        deviceManager->setCurrentRound(-1);
        deviceManager->setNewModelState(ModelState_IDLE);
        
        auto result = deviceManager->saveDeviceConfig();
        if (result.isSuccess()) {
            LOG_INFO("Federated state reset successfully");
            Serial.println("Federated state reset");
        } else {
            LOG_ERROR_CODE(result.errorCode, "Failed to save device config after state reset");
            Serial.println("Warning: State reset but failed to save configuration");
        }
        break;
    }
        
    case 99:
        LOG_DEBUG("Reprinting instructions");
        printInstructions();
        break;
        
    default:
        LOG_WARN("Invalid system command option: %d", option);
        Serial.println("Invalid option");
        break;
    }
}

void SerialInterface::handleTestAndPrediction() {
    LOG_INFO("Starting test data prediction");
    auto modelManager = deviceManager->getModelManager();
    auto currentModel = modelManager->getCurrentModel();
    
    if (!currentModel) {
        LOG_WARN("No current model available for prediction");
        Serial.println("No current model available for prediction");
        return;
    }
    
    LOG_DEBUG("Reading test data from storage");
    testData* test = modelManager->readTestData(modelManager->getLocalModelConfig());
    if (test != nullptr) {
        LOG_DEBUG("Test data loaded successfully, generating prediction");
        IDFLOAT* prediction = modelManager->predictFromCurrentModel(test->x);
        if (prediction) {
            LOG_INFO("Prediction generated successfully");
            Serial.print("Prediction: ");
            int outputSize = currentModel->layers[currentModel->numberOflayers - 1]._numberOfOutputs;
            for (int j = 0; j < outputSize; j++) {
                Serial.print(prediction[j], 0);
                Serial.print("=");
                Serial.print(test->y[j], 0);
                if (j < outputSize - 1) {
                    Serial.print(" - ");
                }
            }
            Serial.println();
            LOG_DEBUG("Prediction output displayed with %d values", outputSize);
        } else {
            LOG_ERROR("Failed to generate prediction from current model");
            Serial.println("Failed to generate prediction");
        }
        delete test;
        LOG_DEBUG("Test data memory freed");
    } else {
        LOG_ERROR("Failed to read test data from storage");
        Serial.println("Failed to read test data");
    }
}
