#include "ModelUtil.h"

#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include "StreamString.h"
#include <StreamUtils.h>

// -------------- Variables

WiFiClient __espClient;
PubSubClient _client(__espClient);

StreamString incomingPayload = StreamString();
unsigned int *_layers;
int _numberOfLayers;
byte *_actvFunctions;
// TODO Write into file while receiving the payload to avoid using too much memory.
// File tempFile;
// String _clientName;

// -------------- Interface functions

void bootUp(unsigned int* layers, unsigned int numberOfLayers, byte* actvFunctions) {
    _layers = layers;
    _numberOfLayers = numberOfLayers;
    _actvFunctions = actvFunctions;
    // _clientName = clientName;

    Serial.println("Booting up...");

    if (!SPIFFS.begin(false)) {
        Serial.println("Error mounting SPIFFS");
        // SPIFFS not able to intialize the partition, cannot load from flash and naither save to it later
        return;
    }

    if (false and SPIFFS.exists(MODEL_PATH)) {
        if (currentModel != NULL) {
            delete currentModel;
        }
        currentModel = loadModelFromFlash(MODEL_PATH);
    } else {
        // load layer structure (input, hidden_1, hidden_2, hidden_x, output) and activation functions (ReLU, Softmax etc)
        if (currentModel != NULL) {
            delete currentModel;
        }
        currentModel = new NeuralNetwork(_layers, _numberOfLayers, _actvFunctions);
        trainModelFromOriginalDataset(*currentModel, X_TRAIN_PATH, Y_TRAIN_PATH);
    }

    setupMQTT();
}

bool saveModelToFlash(NeuralNetwork& NN, const String file) {
    Serial.println("Saving model to flash...");
    File modelFile = SPIFFS.open(file, "w");
    bool result;
    if (!modelFile) {
        // Error opening file
        result = false;
    } else {
        result = NN.save(modelFile);
    }
    Serial.println("Result: " + String(result));
    // modelFile.close();
    return result;
}

NeuralNetwork* loadModelFromFlash(const String& file) {
    Serial.println("Loading model from flash...");
    File modelFile = SPIFFS.open(file, "r");
    if (!modelFile) {
        // Error opening file
        modelFile.close();
    } else {
        NeuralNetwork* r = new NeuralNetwork(modelFile);
        modelFile.close();
        return r;
    }
    return NULL;
}

model transformDataToModel(Stream& stream) {
    Serial.println("Transforming data to model...");
    JsonDocument doc;
    
    ReadLoggingStream loggingStream(stream, Serial);
    DeserializationError result = deserializeJson(doc, loggingStream);

    // DeserializationError result = deserializeJson(doc, stream);
    if (result != DeserializationError::Ok) {
        Serial.println(result.code());
        Serial.println("JSON failed to deserialize");
        return {NULL, NULL};
    }
    const char* precision = doc["precision"];
    #if defined(USE_64_BIT_DOUBLE)
    if (strcmp(precision, "double") != 0) {
        // error loading the model, precision missmatch
        return {NULL, NULL};
    }
    #else
    if (strcmp(precision, "float") != 0) {
        // error loading the model, precision missmatch
        return {NULL, NULL};
    }
    #endif
    JsonArray biases = doc["biases"];
    JsonArray weights = doc["weights"];
    #if defined(USE_64_BIT_DOUBLE)
    double bias[biases.size()], weight[weights.size()];
    #else
    float bias[biases.size()], weight[weights.size()];
    #endif
    int i = 0;
    for (JsonVariant v : biases) {
        #if defined(USE_64_BIT_DOUBLE)
        bias[i++] = v.as<double>();
        #else
        bias[i++] = v.as<float>();
        #endif
    }
    i = 0;
    for (JsonVariant v : weights) {
        #if defined(USE_64_BIT_DOUBLE)
        weight[i++] = v.as<double>();
        #else
        weight[i++] = v.as<float>();
        #endif
    }
    return {bias, weight};
}

bool trainModelFromOriginalDataset(NeuralNetwork& NN, const String& x_file, const String& y_file) {
    Serial.println("Training model from original dataset...");
    for (int t = 0; t < EPOCHS; t++) {
        Serial.println("Epoch: " + String(t));
        File xFile = SPIFFS.open(x_file, "r");
        File yFile = SPIFFS.open(y_file, "r");
        if (!xFile || !yFile) {
            // Error opening file
            return false;
        }
        int len = NN.layers[0]._numberOfInputs;

        // Read from file
        char str[1024];
        char *values;
        #if defined(USE_64_BIT_DOUBLE)
        double val;
        double x[len * BATCH_SIZE], y[len * BATCH_SIZE];
        #else
        float val;
        float x[len * BATCH_SIZE], y[len * BATCH_SIZE];
        #endif
        while (xFile.available() && yFile.available()) {
            int k = 0, j = 0;
            for (int i = 0; i < BATCH_SIZE; i++) {
                size_t bytes_read = xFile.readBytesUntil('\n', str, 1023);
                if (bytes_read < 1) {
                    break;
                }
                str[bytes_read] = '\0';
                // Serial.println(str);
        
                values = strtok(str, ",");
                while (values != NULL) {
                    #if defined(USE_64_BIT_DOUBLE)
                    val = strtod(values, NULL);
                    #else
                    val = strtof(values, NULL);
                    #endif
                    x[j % (len * BATCH_SIZE)] = val;
                    j++;
                    if (j % (len * BATCH_SIZE) == 0)
                        j = 0;
                    values = strtok(NULL, ",");
                }
        
                bytes_read = yFile.readBytesUntil('\n', str, 1023);
                if (bytes_read < 1) {
                    break;
                }
                str[bytes_read] = '\0';
        
                values = strtok(str, ",");
                j = 0;
                while (values != NULL) {
                    #if defined(USE_64_BIT_DOUBLE)
                    val = strtod(values, NULL);
                    #else
                    val = strtof(values, NULL);
                    #endif
                    y[k % (len * BATCH_SIZE)] = val;
                    k++;
                    if (k % (len * BATCH_SIZE) == 0)
                        k = 0;
                    values = strtok(NULL, ",");
                }
            }

            // Train model
            NN.FeedForward(x);
            NN.BackProp(y);
            NN.getMeanSqrdError(BATCH_SIZE);
            // Serial.println(NN.getMeanSqrdError(BATCH_SIZE));
        }
        NN.print();
        
        xFile.close();
        yFile.close();
    }

    return true;
}

void processMessages() {
    _client.loop();
}


// -------------- Local functions

void setupMQTT() {
    Serial.println("Setting up MQTT...");
    _client.setServer(MQTT_BROKER, 1883);
    _client.setCallback(mqttCallback);
    connectToWifi(true);
    connectToServerMQTT(true);
}

bool connectToWifi(bool forever) {
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("Already connected to Wifi");
        return true;
    } else {
        Serial.println("Connecting to wifi...");
        WiFi.begin(WIFI_SSID, WIFI_PASSOWRD);
        while (WiFi.status() != WL_CONNECTED || forever) {
            delay(500);
            if (WiFi.status() == WL_CONNECTED) {
                Serial.println("Wifi connected");
                Serial.println("IP address: " + WiFi.localIP().toString());
                return true;
            }
        }
        Serial.println("Failed to connect to Wifi");
        // TODO: Need to handle wifi connection in case of error
        return false;
    }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
    // handle incoming messages

    if (strcmp(topic, MQTT_TOPIC) == 0) {
        if (String((char*)payload).startsWith("INICIO_TRANSMISSAO")) {
            Serial.println("Starting transmission...");
            incomingPayload.clear();
        } else if (String((char*)payload).startsWith("FIM_TRANSMISSAO")) {
            Serial.println("Ending transmission...");
            // Serial.println(incomingPayload);
            model m = transformDataToModel(incomingPayload);
            if (m.biases != NULL && m.weights != NULL) {
                Serial.println("Model parsed successfully...");
                if (newModel != NULL) {
                    delete newModel;
                }
                for (int i = 0; i < sizeof(m.weights); i++) {
                    Serial.println(m.weights[i]);
                }
                for (int i = 0; i < sizeof(m.biases); i++) {
                    Serial.println(m.biases[i]);
                }
                newModel = new NeuralNetwork(_layers, m.weights, m.biases, _numberOfLayers, _actvFunctions);
                trainNewModel = true;
            }
        } else {
            // TODO should write into buffer file instead of storing all in memory to avoid issues with big files being received
            Serial.println("Payload received...");
            incomingPayload.concat((char*)payload, length);
        }
    }
}

bool connectToServerMQTT(bool forever) {
    if(_client.connected()) {
        Serial.println("Already connected to server MQTT");
        return true;
    } else {
        Serial.println("Connecting to server MQTT...");
        while (!_client.connected() || forever) {
            if (_client.connect(String(CLIENT_NAME).c_str())) {
                _client.subscribe(MQTT_TOPIC);
                Serial.println("Connected to server MQTT");
                return true;
            }
        }
        Serial.println("Failed to connect to server MQTT");
        // TODO: Need to handle unable to connect to server
        return false;
    }
}

void sendModelToNetwork(NeuralNetwork& NN) {
    
    Serial.println("Sending model to the network...");
    
    _client.publish(MQTT_TOPIC, "INICIO_TRANSMISSAO");
    delay(100);
    JsonDocument doc;

    #if defined(USE_64_BIT_DOUBLE)
    doc["precision"] = "double";
    #else
    doc["precision"] = "float";
    #endif

    doc["biases"] = JsonArray();
    doc["weights"] = JsonArray();

    for(unsigned int n=0; n<NN.numberOflayers; n++){
        for(unsigned int i=0; i<NN.layers[n]._numberOfOutputs; i++) {
            #if defined(USE_64_BIT_DOUBLE)
            doc["biases"].add(String(NN.layers[n].bias[i], 16));
            #else
            doc["biases"].add(NN.layers[n].bias[i]);
            #endif
            for(unsigned int j=0; j<NN.layers[n]._numberOfInputs; j++){
                #if defined(USE_64_BIT_DOUBLE)
                doc["weights"].add(String(NN.layers[n].weights[i][j], 16));
                #else
                doc["weights"].add(NN.layers[n].weights[i][j]);
                #endif
            }
        }
    }

    StreamString stream = StreamString();
    size_t len = serializeJson(doc, stream);

    char buffer[MQTT_MESSAGE_SIZE];
    size_t bytesRemaining = len;
    size_t currentPosition = 0;
    
    while (bytesRemaining > 0) {
        size_t bytesToRead = min((size_t) MQTT_MESSAGE_SIZE - 1, bytesRemaining);
        size_t bytes_read = stream.readBytes(buffer, bytesToRead);
        buffer[bytes_read] = '\0';
        
        _client.publish(MQTT_TOPIC, buffer);
        bytesRemaining -= bytes_read;
        currentPosition += bytes_read;
        
        delay(100);
    }

    Serial.println("Model sent to the network...");
    _client.publish(MQTT_TOPIC, "FIM_TRANSMISSAO");

//    Serial.println(stream);


}

// -------------- Unimplemeneted
/*
void receiveModelFromNetwork() {
    File modelFile = SPIFFS.open(NEW_MODEL_PATH, "w");
    transformDataToModel(modelFile);
    modelFile.close();
}

void predictFromCurrentModel() {

}

void calculateMetricsFromModel() {

}


void readDataFromSensors() {

}

void writeDataToDatabase() {

}

*/