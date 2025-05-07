
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
#define X_TRAIN_PATH "/x_train_4.csv"
#define Y_TRAIN_PATH "/y_train_4.csv"
#define X_TEST_PATH "/x_test_4.csv"
#define Y_TEST_PATH "/y_test_4.csv"
#define GATHERED_DATA_PATH "/data.db"
#define CLIENT_NAME "esp04"
#define MQTT_PUBLISH_TOPIC "esp32/fl/model/push"
#define MQTT_RECEIVE_TOPIC "esp32/fl/model/pull"
#define MQTT_RECEIVE_COMMANDS_TOPIC "esp32/fl/commands/pull"
#define MQTT_SEND_COMMANDS_TOPIC "esp32/fl/commands/push"
// #define BATCH_SIZE 8
#define EPOCHS 1
#define WIFI_SSID "Redmi Note 8"
#define WIFI_PASSWORD "456123789a"
#define MQTT_BROKER "192.168.43.235"
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
    DFLOAT *biases;
    DFLOAT *weights;
    unsigned long parsingTime = 0;
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
    ModelState_READY_TO_TRAIN,
    ModelState_DONE_TRAINING,
    ModelState_MODEL_BUSY,
};

enum FederateState {
    FederateState_NONE,
    FederateState_SUBSCRIBED,
    FederateState_TRAINING,
    FederateState_DONE
};

enum FederateCommand {
    FederateCommand_JOIN,
    FederateCommand_READY,
    FederateCommand_LEAVE,
};
/*
class MetricsCollection {
    private:
        std::vector<multiClassClassifierMetrics*> metrics;
        
    public:
        ~MetricsCollection() {
            clear();  // Auto-cleanup
        }
        
        void add(multiClassClassifierMetrics* item) {
            metrics.push_back(item);
        }
        
        multiClassClassifierMetrics* get(size_t index) {
            return (index < metrics.size()) ? metrics[index] : nullptr;
        }
        
        bool remove(size_t index) {
            if (index >= metrics.size()) return false;
            delete metrics[index];
            metrics.erase(metrics.begin() + index);
            return true;
        }
        
        size_t size() const {
            return metrics.size();
        }
        
        bool isEmpty() const {
            return metrics.empty();
        }
        
        void clear() {
            for (auto m : metrics) {
                delete m;
            }
            metrics.clear();
        }
};
*/
ModelState newModelState = ModelState_IDLE;
FederateState fedareState = FederateState_NONE;
NeuralNetwork* newModel = NULL;
NeuralNetwork* currentModel = NULL;
multiClassClassifierMetrics* currentModelMetrics = NULL;
multiClassClassifierMetrics* newModelMetrics = NULL;

#ifdef PARALLEL
// SemaphoreHandle_t xSemaphoreCurrentModel = NULL;
#endif

void bootUp(unsigned int* layers, unsigned int numberOfLayers, byte* actvFunctions);

void bootUp(unsigned int* layers, unsigned int numberOfLayers, byte* actvFunctions, DFLOAT learningRateOfWeights, DFLOAT learningRateOfBiases);

bool saveModelToFlash(NeuralNetwork& NN, const String file);

NeuralNetwork* loadModelFromFlash(const String& file);

model* transformDataToModel(Stream& stream);

multiClassClassifierMetrics* trainModelFromOriginalDataset(NeuralNetwork& NN, const String& x_file, const String& y_file);

void sendModelToNetwork(NeuralNetwork& NN, multiClassClassifierMetrics& metrics);

void sendMessageToNetwork(FederateCommand command);

void processMessages();

DFLOAT* predictFromCurrentModel(DFLOAT* x);

testData* readTestData();

// --------------


void setupMQTT();
bool connectToWifi(bool forever = true);
// void mqttCallback(char* topic, byte* payload, unsigned int length);
bool connectToServerMQTT();

#endif /* MODELUTIL_H_ */