#include "ModelManager.h"
#include <ArduinoJson.h>
#include <LittleFS.h>

ModelManager::ModelManager() {
    currentModel = nullptr;
    newModel = nullptr;
    currentModelMetrics = nullptr;
    newModelMetrics = nullptr;
    localModelConfig = nullptr;
    federateModelConfig = nullptr;
    datasetSize = 0;
}

ModelManager::~ModelManager() {
    cleanupModels();
}

void ModelManager::cleanupModels() {
    if (currentModel) {
        delete currentModel;
        currentModel = nullptr;
    }
    if (newModel) {
        delete newModel;
        newModel = nullptr;
    }
    if (currentModelMetrics) {
        delete currentModelMetrics;
        currentModelMetrics = nullptr;
    }
    if (newModelMetrics) {
        delete newModelMetrics;
        newModelMetrics = nullptr;
    }
    if (localModelConfig) {
        delete localModelConfig;
        localModelConfig = nullptr;
    }
    if (federateModelConfig) {
        delete federateModelConfig;
        federateModelConfig = nullptr;
    }
}

void ModelManager::initializeModels(bool initBaseModel) {
    if (initBaseModel && localModelConfig) {
        LOG_INFO("Initializing base model with %d layers", localModelConfig->numberOfLayers);
        currentModel = new NeuralNetwork(localModelConfig->layers, localModelConfig->numberOfLayers, 
                                        localModelConfig->actvFunctions);
        currentModel->LearningRateOfBiases = localModelConfig->learningRateOfBiases;
        currentModel->LearningRateOfWeights = localModelConfig->learningRateOfWeights;
        LOG_DEBUG("Base model initialized successfully");
    } else {
        LOG_DEBUG("Skipping base model initialization - not requested or no config");
    }
}

void ModelManager::setupFederatedModel() {
    LOG_INFO("Setting up federated model");
    
    if (newModel != nullptr) {
        LOG_DEBUG("Deleting existing new model");
        delete newModel;
        newModel = nullptr;
    }
    
    if (federateModelConfig) {
        LOG_INFO("Creating federated model with %d layers", federateModelConfig->numberOfLayers);
        newModel = new NeuralNetwork(federateModelConfig->layers, federateModelConfig->numberOfLayers, 
                                    federateModelConfig->actvFunctions);
        newModel->LearningRateOfBiases = federateModelConfig->learningRateOfBiases;
        newModel->LearningRateOfWeights = federateModelConfig->learningRateOfWeights;
        LOG_INFO("Federated model created successfully");
    } else {
        LOG_ERROR("Cannot setup federated model: No federate model config available");
    }
}

ErrorResult ModelManager::saveModelToFlash(NeuralNetwork& NN, const String& file) {
    LOG_INFO("Saving model to flash: %s", file.c_str());
    
    File modelFile = LittleFS.open(file, "w");
    if (!modelFile) {
        LOG_ERROR_CODE(ERR_FS_OPEN_FAILED, "Failed to open file for writing: %s", file.c_str());
        return ErrorResult(ERR_FS_OPEN_FAILED, "Failed to open model file for writing");
    }
    
    bool result = NN.save(modelFile);
    modelFile.close();
    
    if (result) {
        LOG_INFO("Model saved successfully to %s", file.c_str());
        return ErrorResult::success();
    } else {
        LOG_ERROR_CODE(ERR_FS_WRITE_FAILED, "Model save operation failed for file: %s", file.c_str());
        return ErrorResult(ERR_FS_WRITE_FAILED, "Model save operation failed");
    }
}

NeuralNetwork* ModelManager::loadModelFromFlash(const String& file) {
    LOG_INFO("Loading model from flash: %s", file.c_str());
    
    if (!LittleFS.exists(file)) {
        LOG_WARN("Model file does not exist: %s", file.c_str());
        return nullptr;
    }
    
    File modelFile = LittleFS.open(file, "r");
    if (!modelFile) {
        LOG_ERROR_CODE(ERR_FS_OPEN_FAILED, "Failed to open file for reading: %s", file.c_str());
        return nullptr;
    }
    
    try {
        NeuralNetwork* loadedModel = new NeuralNetwork(federateModelConfig->layers, federateModelConfig->numberOfLayers, 
                          federateModelConfig->actvFunctions);
        loadedModel->load(modelFile);
        loadedModel->LearningRateOfBiases = federateModelConfig->learningRateOfBiases;
        loadedModel->LearningRateOfWeights = federateModelConfig->learningRateOfWeights;
        modelFile.close();
        LOG_INFO("Model loaded successfully from: %s", file.c_str());
        return loadedModel;
    } catch (...) {
        modelFile.close();
        LOG_ERROR_CODE(ERR_MODEL_LOAD_FAILED, "Failed to load model from: %s", file.c_str());
        return nullptr;
    }
}

model* ModelManager::transformDataToModel(Stream& stream) {
    LOG_INFO("Transforming received data stream to model");
    
    unsigned long startTime = millis();
    JsonDocument doc;
    
    DeserializationError result = deserializeJson(doc, stream);
    doc.shrinkToFit();
    
    if (result != DeserializationError::Ok) {
        LOG_ERROR_CODE(ERR_NETWORK_PARSE_FAILED, "Failed to parse JSON from network stream: %s", result.c_str());
        return nullptr;
    }
    
    const char* precision = doc["precision"];
#if defined(USE_64_BIT_DOUBLE)
    if (strcmp(precision, "double") != 0) {
        LOG_ERROR("Precision mismatch - expected double, received: %s", precision ? precision : "null");
        return nullptr;
    }
#else
    if (strcmp(precision, "float") != 0) {
        LOG_ERROR("Precision mismatch - expected float, received: %s", precision ? precision : "null");
        return nullptr;
    }
#endif
    
    JsonArray biases = doc["biases"];
    JsonArray weights = doc["weights"];
    IDFLOAT* bias = new IDFLOAT[biases.size()];
    IDFLOAT* weight = new IDFLOAT[weights.size()];
    
    LOG_DEBUG("Processing %d biases and %d weights", biases.size(), weights.size());
    
    for (int i = 0; i < biases.size(); i++) {
        bias[i] = biases[i].as<IDFLOAT>();
    }
    
    for (int i = 0; i < weights.size(); i++) {
        weight[i] = weights[i].as<IDFLOAT>();
    }
    
    model* m = new model;
    m->biases = bias;
    m->weights = weight;
    m->parsingTime = millis() - startTime;
    m->round = -1;
    if (doc["round"].is<int>()) {
        m->round = doc["round"].as<int>();
    }
    
    LOG_INFO("Model transformation complete - parsing took %lu ms, round: %d", m->parsingTime, m->round);
    return m;
}

multiClassClassifierMetrics* ModelManager::trainModelFromOriginalDataset(NeuralNetwork& NN, ModelConfig& config, 
                                                                         const String& x_file, const String& y_file) {
    LOG_INFO("Starting model training from dataset files: %s, %s", x_file.c_str(), y_file.c_str());
    
    unsigned long initTime = millis();
    datasetSize = 0;
    
    File xFile = LittleFS.open(x_file, "r");
    File yFile = LittleFS.open(y_file, "r");
    
    if (!xFile || !yFile) {
        LOG_ERROR_CODE(ERR_FS_OPEN_FAILED, "Failed to open training files - X: %s, Y: %s", 
                      xFile ? "OK" : "FAILED", yFile ? "OK" : "FAILED");
        if (xFile) xFile.close();
        if (yFile) yFile.close();
        return nullptr;
    }
    
    String xLine, yLine;
    IDFLOAT x[NN.layers[0]._numberOfInputs], y[NN.layers[NN.numberOflayers - 1]._numberOfOutputs];
    
    multiClassClassifierMetrics* metrics = new multiClassClassifierMetrics;
    metrics->numberOfClasses = NN.layers[NN.numberOflayers - 1]._numberOfOutputs;
    metrics->metrics = new classClassifierMetricts[metrics->numberOfClasses];
    
    LOG_INFO("Training for %d epochs with %d input features and %d output classes", 
             config.epochs, NN.layers[0]._numberOfInputs, metrics->numberOfClasses);
    
    for (int t = 0; t < config.epochs; t++) {
        LOG_DEBUG("Starting epoch %d/%d", t + 1, config.epochs);
        
        xFile.seek(0);
        yFile.seek(0);
        
        int samplesProcessed = 0;
        while (xFile.available() && yFile.available()) {
            xLine = xFile.readStringUntil('\n');
            yLine = yFile.readStringUntil('\n');
            
            if (xLine.length() == 0 || yLine.length() == 0) break;
            
            // Parse CSV line for x
            int xIndex = 0;
            int startIndex = 0;
            for (int i = 0; i <= xLine.length() && xIndex < NN.layers[0]._numberOfInputs; i++) {
                if (i == xLine.length() || xLine.charAt(i) == ',') {
                    String value = xLine.substring(startIndex, i);
                    x[xIndex++] = value.toFloat();
                    startIndex = i + 1;
                }
            }
            
            // Parse CSV line for y
            int yIndex = 0;
            startIndex = 0;
            for (int i = 0; i <= yLine.length() && yIndex < NN.layers[NN.numberOflayers - 1]._numberOfOutputs; i++) {
                if (i == yLine.length() || yLine.charAt(i) == ',') {
                    String value = yLine.substring(startIndex, i);
                    y[yIndex++] = value.toFloat();
                    startIndex = i + 1;
                }
            }
            
            // Train on this sample
            NN.FeedForward(x);
            NN.BackProp(y);
            
            if (t == 0) { // Only count dataset size on first epoch
                datasetSize++;
            }
            samplesProcessed++;
        }
        
        if (t == 0) {
            LOG_DEBUG("Epoch %d complete - processed %d samples", t + 1, samplesProcessed);
        }
    }
    
    metrics->trainingTime = millis() - initTime;
    metrics->epochs = config.epochs;
    
    xFile.close();
    yFile.close();
    
    LOG_INFO("Training complete - %d epochs, %lu samples, %lu ms total time", 
             config.epochs, datasetSize, metrics->trainingTime);
    return metrics;
}

testData* ModelManager::readTestData(ModelConfig* modelConfig) {
    if (!modelConfig) {
        LOG_ERROR("Cannot read test data: No model config provided");
        return nullptr;
    }
    
    LOG_DEBUG("Reading test data from %s and %s", X_TEST_PATH, Y_TEST_PATH);
    
    File xFile = LittleFS.open(X_TEST_PATH, "r");
    File yFile = LittleFS.open(Y_TEST_PATH, "r");
    
    if (!xFile || !yFile) {
        LOG_ERROR_CODE(ERR_FS_OPEN_FAILED, "Failed to open test files - X: %s, Y: %s", 
                      xFile ? "OK" : "FAILED", yFile ? "OK" : "FAILED");
        if (xFile) xFile.close();
        if (yFile) yFile.close();
        return nullptr;
    }
    
    testData* data = new testData;
    data->x = new DFLOAT[modelConfig->layers[0]];
    data->y = new DFLOAT[modelConfig->layers[modelConfig->numberOfLayers - 1]];
    
    // Read first line from both files
    String xLine = xFile.readStringUntil('\n');
    String yLine = yFile.readStringUntil('\n');
    
    LOG_DEBUG("Parsing test sample with %d inputs and %d outputs", 
              modelConfig->layers[0], modelConfig->layers[modelConfig->numberOfLayers - 1]);
    
    // Parse x data
    int xIndex = 0;
    int startIndex = 0;
    for (int i = 0; i <= xLine.length() && xIndex < modelConfig->layers[0]; i++) {
        if (i == xLine.length() || xLine.charAt(i) == ',') {
            String value = xLine.substring(startIndex, i);
            data->x[xIndex++] = value.toFloat();
            startIndex = i + 1;
        }
    }
    
    // Parse y data
    int yIndex = 0;
    startIndex = 0;
    for (int i = 0; i <= yLine.length() && yIndex < modelConfig->layers[modelConfig->numberOfLayers - 1]; i++) {
        if (i == yLine.length() || yLine.charAt(i) == ',') {
            String value = yLine.substring(startIndex, i);
            data->y[yIndex++] = value.toFloat();
            startIndex = i + 1;
        }
    }
    
    xFile.close();
    yFile.close();
    
    LOG_DEBUG("Test data loaded successfully - %d inputs, %d outputs", xIndex, yIndex);
    return data;
}

DFLOAT* ModelManager::predictFromCurrentModel(DFLOAT* x) {
    if (!currentModel) {
        LOG_WARN("Cannot predict: No current model loaded");
        return nullptr;
    }
    
    LOG_DEBUG("Running prediction on current model");
    return currentModel->FeedForward(x);
}

bool ModelManager::compareMetrics(multiClassClassifierMetrics* oldMetrics, multiClassClassifierMetrics* newMetrics) {
    if (!oldMetrics || !newMetrics) return false;
    
    // Simple comparison based on accuracy
    return newMetrics->accuracy() > oldMetrics->accuracy();
}

void ModelManager::setCurrentModelMetrics(multiClassClassifierMetrics* metrics) {
    if (currentModelMetrics) {
        delete currentModelMetrics;
    }
    currentModelMetrics = metrics;
}

void ModelManager::setNewModelMetrics(multiClassClassifierMetrics* metrics) {
    if (newModelMetrics) {
        delete newModelMetrics;
    }
    newModelMetrics = metrics;
}
