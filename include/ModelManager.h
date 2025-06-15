#ifndef MODEL_MANAGER_H_
#define MODEL_MANAGER_H_

#include "Config.h"
#include "Types.h"
#include <NeuralNetwork.h>
#include <LittleFS.h>

class ModelManager {
private:
    NeuralNetwork* currentModel;
    NeuralNetwork* newModel;
    multiClassClassifierMetrics* currentModelMetrics;
    multiClassClassifierMetrics* newModelMetrics;
    ModelConfig* localModelConfig;
    ModelConfig* federateModelConfig;
    
    unsigned long datasetSize;

public:
    ModelManager();
    ~ModelManager();
    
    // Model lifecycle
    void initializeModels(bool initBaseModel = true);
    void setupFederatedModel();
    void cleanupModels();
    
    // File I/O
    ErrorResult saveModelToFlash(NeuralNetwork& NN, const String& file);
    NeuralNetwork* loadModelFromFlash(const String& file);
    
    // Data processing
    model* transformDataToModel(Stream& stream);
    multiClassClassifierMetrics* trainModelFromOriginalDataset(NeuralNetwork& NN, ModelConfig& config, 
                                                              const String& x_file, const String& y_file);
    testData* readTestData(ModelConfig* modelConfig);
    
    // Prediction
    DFLOAT* predictFromCurrentModel(DFLOAT* x);
    
    // Model comparison
    bool compareMetrics(multiClassClassifierMetrics* oldMetrics, multiClassClassifierMetrics* newMetrics);
    
    // Getters
    NeuralNetwork* getCurrentModel() { return currentModel; }
    NeuralNetwork* getNewModel() { return newModel; }
    multiClassClassifierMetrics* getCurrentModelMetrics() { return currentModelMetrics; }
    multiClassClassifierMetrics* getNewModelMetrics() { return newModelMetrics; }
    ModelConfig* getLocalModelConfig() { return localModelConfig; }
    ModelConfig* getFederateModelConfig() { return federateModelConfig; }
    unsigned long getDatasetSize() { return datasetSize; }
    
    // Setters
    void setCurrentModel(NeuralNetwork* model) { currentModel = model; }
    void setNewModel(NeuralNetwork* model) { newModel = model; }
    void setCurrentModelMetrics(multiClassClassifierMetrics* metrics);
    void setNewModelMetrics(multiClassClassifierMetrics* metrics);
    void setLocalModelConfig(ModelConfig* config) { localModelConfig = config; }
    void setFederateModelConfig(ModelConfig* config) { federateModelConfig = config; }
};

#endif /* MODEL_MANAGER_H_ */
