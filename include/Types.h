#ifndef TYPES_H_
#define TYPES_H_

#include <NeuralNetwork.h>

// Data structures
struct model {
    IDFLOAT *biases;
    IDFLOAT *weights;
    unsigned long parsingTime = 0;
    int round;
    
    ~model() {
        delete[] biases;
        delete[] weights;
    }
};

struct classClassifierMetricts {
    unsigned int truePositives = 0;
    unsigned int trueNegatives = 0;
    unsigned int falsePositives = 0;
    unsigned int falseNegatives = 0;

    DFLOAT totalPredictions();
    DFLOAT accuracy();
    DFLOAT precision();
    DFLOAT recall();
    DFLOAT f1Score();
};

struct multiClassClassifierMetrics {
    classClassifierMetricts* metrics;
    unsigned int numberOfClasses;
    DFLOAT meanSqrdError;
    unsigned long parsingTime = 0;
    unsigned long trainingTime = 0;
    unsigned long epochs = 0;

    DFLOAT totalPredictions();
    DFLOAT accuracy();
    DFLOAT precision();
    DFLOAT recall();
    DFLOAT f1Score();
    DFLOAT balancedAccuracy();
    DFLOAT balancedPrecision();
    DFLOAT balancedRecall();
    DFLOAT balancedF1Score();
    void print();
    
    ~multiClassClassifierMetrics() {
        delete[] metrics;
    }
};

struct testData {
    DFLOAT* x;
    DFLOAT* y;

    ~testData() {
        delete[] x;
        delete[] y;
    }
};

// Enums
enum ModelState {
    ModelState_IDLE,
    ModelState_WAITING_DOWNLOAD,
    ModelState_READY_TO_TRAIN,
    ModelState_DONE_TRAINING,
    ModelState_MODEL_BUSY,
};

enum FederateState {
    FederateState_NONE,
    FederateState_SUBSCRIBED,
    FederateState_STARTING,
    FederateState_TRAINING,
    FederateState_DONE,
};

enum FederateCommand {
    FederateCommand_JOIN,
    FederateCommand_READY,
    FederateCommand_LEAVE,
    FederateCommand_RESUME,
    FederateCommand_ALIVE,
};

// Memory tracking structures
struct FixedMemoryUsage {
    size_t onBoot;
    size_t loadConfig;
    size_t loadAndTrainModel;
    size_t connectionMade;
    size_t afterFullSetup;
    size_t minFreeHeapAfterSetup;
};

struct RoundMemoryUsage {
    size_t messageReceived;
    size_t beforeTrain;
    size_t afterTrain;
    size_t beforeSend;
    size_t minimumFree;
};

// Configuration structures
struct ModelConfig {
    unsigned int* layers;
    unsigned int numberOfLayers;
    byte* actvFunctions;
    unsigned int epochs = 1;
    DFLOAT learningRateOfWeights = 0.3333;
    DFLOAT learningRateOfBiases = 0.0666;
    unsigned long randomSeed = 10;

    ModelConfig(unsigned int* layers, unsigned int numberOfLayers, byte* actvFunctions, 
               unsigned int epochs = 1, unsigned long randomSeed = 10, 
               DFLOAT learningRateOfWeights = 0.3333f, DFLOAT learningRateOfBiases = 0.0666f)
        : layers(layers), numberOfLayers(numberOfLayers), actvFunctions(actvFunctions), 
          epochs(epochs), randomSeed(randomSeed), learningRateOfWeights(learningRateOfWeights), 
          learningRateOfBiases(learningRateOfBiases) {}
};

struct DeviceConfig {
    int currentRound = -1;
    FederateState currentFederateState = FederateState_NONE;
    ModelState newModelState = ModelState_IDLE;
    multiClassClassifierMetrics* currentModelMetrics = nullptr;
    ModelConfig* loadedFederateModelConfig = nullptr;

    void reset();
    ~DeviceConfig();
};

#endif /* TYPES_H_ */
