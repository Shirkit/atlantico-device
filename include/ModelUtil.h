
#ifndef MODELUTIL_H_
#define MODELUTIL_H_

#include <NeuralNetwork.h>

#define SUPPORTS_SD_FUNCTIONALITY

#if defined(USE_64_BIT_DOUBLE)
#define ARDUINOJSON_USE_DOUBLE 1
#else
#define ARDUINOJSON_USE_DOUBLE 0
#endif

// -------------- Constants

#define MODEL_PATH "/model.nn"
#define NEW_MODEL_PATH "/new_model.nn"
#define X_TRAIN_PATH "/x_train_esp32.csv"
#define Y_TRAIN_PATH "/y_train_esp32.csv"
#define GATHERED_DATA_PATH "/data.db"
#define MQTT_TOPIC "esp32/ai"
#define BATCH_SIZE 1
#define EPOCHS 1
#define WIFI_SSID "PedroRapha"
#define WIFI_PASSOWRD "456123789a"
#define MQTT_BROKER "192.168.15.13"
#define CLIENT_NAME "esp01"
#define MQTT_MESSAGE_SIZE 200

/**
 * Defining the JSON structure for networking messaging
 * {
 *   "precision"    :   "float" | "double",
 *   "biases"       :   float[] | double[],
 *   "weights"      :   float[] | double[],
 *   }
 * }
 */

struct model {
    #if defined(USE_64_BIT_DOUBLE)
    double *biases;
    double *weights;
    #else
    float *biases;
    float *weights;
    #endif
};

bool trainNewModel = false;
NeuralNetwork* newModel;
NeuralNetwork* currentModel;

void bootUp(unsigned int* layers, unsigned int numberOfLayers, byte* actvFunctions);

bool saveModelToFlash(NeuralNetwork& NN, const String file);

NeuralNetwork* loadModelFromFlash(const String& file);

model transformDataToModel(Stream& stream);

bool trainModelFromOriginalDataset(NeuralNetwork& NN, const String& x_file, const String& y_file);

void processMessages();

// --------------


void setupMQTT();
bool connectToWifi(bool forever = true);
void mqttCallback(char* topic, byte* payload, unsigned int length);
bool connectToServerMQTT(bool forever = true);

#endif /* MODELUTIL_H_ */