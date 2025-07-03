
#ifndef MODELUTIL_H_
#define MODELUTIL_H_

#include <NeuralNetwork.h>

#define SUPPORTS_SD_FUNCTIONALITY

#if defined(USE_64_BIT_DOUBLE)
#define ARDUINOJSON_USE_DOUBLE 1
#else
#define ARDUINOJSON_USE_DOUBLE 0
#endif

#if DEBUG
#define D_SerialBegin(...) Serial.begin(__VA_ARGS__);
#define D_print(...)    Serial.print(__VA_ARGS__)
#define D_write(...)    Serial.write(__VA_ARGS__)
#define D_println(...)  Serial.println(__VA_ARGS__)
#define D_printf(...)   Serial.printf(__VA_ARGS__)
#define printTiming(...)   printTiming(__VA_ARGS__)
#define printMemory(...)   printMemory(__VA_ARGS__)
#else
#define D_SerialBegin(...)
#define D_print(...)
#define D_write(...)
#define D_println(...)
#define printTiming(...)
#define D_printf(...)
#endif

// -------------- Constants

#define MODEL_PATH "/model.nn"
#define NEW_MODEL_PATH "/new_model.nn"
#define TEMPORARY_NEW_MODEL_PATH "/new_model_temp.nn"
#define CONFIGURATION_PATH "/config.json"
#define DEVICE_DEFINITION_PATH "/device.json"
#define X_TRAIN_PATH "/x_train.csv"
#define Y_TRAIN_PATH "/y_train.csv"
#define X_TEST_PATH "/x_test.csv"
#define Y_TEST_PATH "/y_test.csv"
#define GATHERED_DATA_PATH "/data.db"
// #define CLIENT_NAME "esp01"
#define MQTT_PUBLISH_TOPIC "esp32/fl/model/push"
#define MQTT_RAW_PUBLISH_TOPIC "esp32/fl/model/rawpush"
#define MQTT_RECEIVE_TOPIC "esp32/fl/model/pull"
#define MQTT_RAW_RECEIVE_TOPIC "esp32/fl/model/rawpull"
#define MQTT_RESUME_TOPIC "esp32/fl/model/resume"
#define MQTT_RAW_RESUME_TOPIC "esp32/fl/model/rawresume"
#define MQTT_RECEIVE_COMMANDS_TOPIC "esp32/fl/commands/pull"
#define MQTT_SEND_COMMANDS_TOPIC "esp32/fl/commands/push"
#define WIFI_SSID "PedroRapha"
#define WIFI_PASSWORD "456123789a"
#define MQTT_BROKER "192.168.15.9"
// #define MQTT_MESSAGE_SIZE 500
#define CONNECTION_TIMEOUT 30000 // in miliseconds

/**
 * Defining the JSON structure for networking messaging
 * {
 *   "precision"    :   "float" | "double",
 *   "client"       :   string,
 *   "biases"       :   float[] | double[],
 *   "weights"      :   float[] | double[],
 *   }
 * }
 */

struct model {
    IDFLOAT *biases;
    IDFLOAT *weights;
    unsigned long parsingTime = 0;
    int round;
    
    model() : biases(nullptr), weights(nullptr), round(-1) {}
    
    ~model() {
        if (biases != nullptr) {
            delete[] biases;
        }
        if (weights != nullptr) {
            delete[] weights;
        }
    }
};

struct classClassifierMetricts {
    unsigned int truePositives = 0;
    unsigned int trueNegatives = 0;
    unsigned int falsePositives = 0;
    unsigned int falseNegatives = 0;

    DFLOAT totalPredictions() {
        return truePositives + falseNegatives;
    }

    DFLOAT accuracy() {
        return ((DFLOAT) truePositives + trueNegatives) / ((DFLOAT) (truePositives + trueNegatives + falsePositives + falseNegatives));
    }

    DFLOAT precision() {
        DFLOAT precision = (DFLOAT) truePositives / (truePositives + falsePositives);
        if (precision != precision)
            return 0;
        return precision;
    }

    DFLOAT recall() {
        DFLOAT recall = (DFLOAT) truePositives / (truePositives + falseNegatives);
        if (recall != recall)
            return 0;
        return recall;
    }

    DFLOAT f1Score() {
        DFLOAT f1Score = (DFLOAT) 2 * (precision() * recall()) / (precision() + recall());
        if (f1Score != f1Score)
            return 0;
        return f1Score;
    }
};

struct multiClassClassifierMetrics {
    classClassifierMetricts* metrics;
    unsigned int numberOfClasses;
    DFLOAT meanSqrdError;
    unsigned long parsingTime = 0;
    unsigned long trainingTime = 0;
    unsigned long epochs = 0;

    DFLOAT totalPredictions() {
        DFLOAT sum = 0;
        for (unsigned int i = 0; i < numberOfClasses; i++) {
            sum += metrics[i].truePositives + metrics[i].falseNegatives;
        }
        return sum;
    }

    DFLOAT accuracy() {
        DFLOAT sum = 0;
        for (unsigned int i = 0; i < numberOfClasses; i++) {
            sum += metrics[i].accuracy();
        }
        return sum / numberOfClasses;
    }

    DFLOAT precision() {
        DFLOAT sum = 0;
        for (unsigned int i = 0; i < numberOfClasses; i++) {
            sum += metrics[i].precision();
        }
        return sum / numberOfClasses;
    }

    DFLOAT recall() {
        DFLOAT sum = 0;
        for (unsigned int i = 0; i < numberOfClasses; i++) {
            sum += metrics[i].recall();
        }
        return sum / numberOfClasses;
    }

    DFLOAT f1Score() {
        DFLOAT sum = 0;
        for (unsigned int i = 0; i < numberOfClasses; i++) {
            sum += metrics[i].f1Score();
        }
        return sum / numberOfClasses;
    }

    DFLOAT balancedAccuracy() {
        DFLOAT sum = 0;
        for (unsigned int i = 0; i < numberOfClasses; i++) {
            sum += metrics[i].accuracy() * (metrics[i].totalPredictions() / totalPredictions());
        }
        return sum;
    }

    DFLOAT balancedPrecision() {
        DFLOAT sum = 0;
        for (unsigned int i = 0; i < numberOfClasses; i++) {
            sum += metrics[i].precision() * (metrics[i].totalPredictions() / totalPredictions());
        }
        return sum;
    }

    DFLOAT balancedRecall() {
        DFLOAT sum = 0;
        for (unsigned int i = 0; i < numberOfClasses; i++) {
            sum += metrics[i].recall() * (metrics[i].totalPredictions() / totalPredictions());
        }
        return sum;
    }

    DFLOAT balancedF1Score() {
        DFLOAT sum = 0;
        for (unsigned int i = 0; i < numberOfClasses; i++) {
            sum += metrics[i].f1Score() * (metrics[i].totalPredictions() / (float) totalPredictions());
        }
        return sum;
    }


    void print() {
        Serial.println("Metrics:");
        Serial.print("Mean Squared Error: ");
        Serial.println(meanSqrdError);
        Serial.print("Accuracy: ");
        Serial.println(accuracy());
        Serial.print("Precision: ");
        Serial.println(precision());
        Serial.print("Recall: ");
        Serial.println(recall());
        Serial.print("F1 Score: ");
        Serial.println(f1Score());
        Serial.println("Class Metrics:");
        for (unsigned int i = 0; i < numberOfClasses; i++) {
            Serial.print("Class ");
            Serial.print(i);
            Serial.println(":");
            Serial.print("True Positives: ");
            Serial.println(metrics[i].truePositives);
            Serial.print("True Negatives: ");
            Serial.println(metrics[i].trueNegatives);
            Serial.print("False Positives: ");
            Serial.println(metrics[i].falsePositives);
            Serial.print("False Negatives: ");
            Serial.println(metrics[i].falseNegatives);
            Serial.print("Accuracy: ");
            Serial.println(metrics[i].accuracy());
            Serial.print("Precision: ");
            Serial.println(metrics[i].precision());
            Serial.print("Recall: ");
            Serial.println(metrics[i].recall());
            Serial.print("F1 Score: ");
            Serial.println(metrics[i].f1Score());
        }
    }

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

struct ModelConfig {
    unsigned int* layers;
    unsigned int numberOfLayers;
    byte* actvFunctions;
    unsigned int epochs = 1;
    DFLOAT learningRateOfWeights = 0.3333;
    DFLOAT learningRateOfBiases = 0.0666;
    unsigned long randomSeed = 10;
    bool jsonWeights = false;

    ModelConfig(unsigned int* layers, unsigned int numberOfLayers, byte* actvFunctions, unsigned int epochs = 1, unsigned long randomSeed = 10, DFLOAT learningRateOfWeights = 0.3333f, DFLOAT learningRateOfBiases = 0.0666f, bool jsonWeights = false)
        : layers(layers), numberOfLayers(numberOfLayers), actvFunctions(actvFunctions), epochs(epochs), randomSeed(randomSeed), learningRateOfWeights(learningRateOfWeights), learningRateOfBiases(learningRateOfBiases), jsonWeights(jsonWeights) {}
};

struct DeviceConfig {
    int currentRound = -1;
    FederateState currentFederateState = FederateState_NONE;
    ModelState newModelState = ModelState_IDLE;
    multiClassClassifierMetrics* currentModelMetrics = nullptr;
    ModelConfig* loadedFederateModelConfig = nullptr;

    void reset() {
        currentRound = -1;
        currentFederateState = FederateState_NONE;
        newModelState = ModelState_IDLE;
        if (currentModelMetrics != nullptr) {
            delete currentModelMetrics;
            currentModelMetrics = nullptr;
        }
    }

    ~DeviceConfig() {
        reset();
    }
};

ModelState newModelState = ModelState_IDLE;
FederateState federateState = FederateState_NONE;
NeuralNetwork* newModel = NULL;
NeuralNetwork* currentModel = NULL;
multiClassClassifierMetrics* currentModelMetrics = NULL;
multiClassClassifierMetrics* newModelMetrics = NULL;
DeviceConfig* deviceConfig = NULL;
FixedMemoryUsage fixedMemoryUsage = {0, 0, 0, 0, 0, 0};
RoundMemoryUsage roundMemoryUsage = {0, 0, 0, 0, 0};
ModelConfig* localModelConfig = NULL;
ModelConfig* federateModelConfig = NULL;
char* CLIENT_NAME;

// void bootUp(unsigned int* layers, unsigned int numberOfLayers, byte* actvFunctions);

// void bootUp(unsigned int* layers, unsigned int numberOfLayers, byte* actvFunctions, DFLOAT learningRateOfWeights, DFLOAT learningRateOfBiases);

void bootUp(bool initBaseModel = true);

bool saveModelToFlash(NeuralNetwork& NN, const String file);

NeuralNetwork* loadModelFromFlash(const String& file);

model* transformDataToModel(Stream& stream);

multiClassClassifierMetrics* trainModelFromOriginalDataset(NeuralNetwork& NN, ModelConfig& config, const String& x_file, const String& y_file);

void sendModelToNetwork(NeuralNetwork& NN, multiClassClassifierMetrics& metrics);

void sendMessageToNetwork(FederateCommand command);

void processMessages();

DFLOAT* predictFromCurrentModel(DFLOAT* x);

testData* readTestData(ModelConfig modelConfig);

void setupMQTT(bool resume = false);

bool connectToWifi(bool forever = true);

bool connectToServerMQTT();

void processModel();

bool compareMetrics(multiClassClassifierMetrics* oldMetrics, multiClassClassifierMetrics* newMetrics);

bool loadDeviceConfig();

bool saveDeviceConfig();

void clearDeviceConfig();

bool loadDeviceDefinitions();

const char* modelStateToString(ModelState state);

#endif /* MODELUTIL_H_ */