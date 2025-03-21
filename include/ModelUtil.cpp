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
unsigned int* _layers;
int _numberOfLayers;
byte* _actvFunctions;
DFLOAT _learningRateOfWeights;
DFLOAT _learningRateOfBiases;
model *tempModel;
// TODO Write into file while receiving the payload to avoid using too much memory.
// String _clientName;

// -------------- Interface functions

bool ensureConnected() {
    if (!WiFi.isConnected()) {
      return connectToWifi(false);
    }
    if (!_client.connected()) {
      return connectToServerMQTT(false);
    }
    return true;
  }

  bool publishWithRetry(const char* topic, const char* payload, int retries = 3) {
    for (int i = 0; i < retries; i++) {
      if (_client.publish(topic, payload)) {
        return true;
      }
      delay(100);
    }
    return false;
  }

void bootUp(unsigned int* layers, unsigned int numberOfLayers, byte* actvFunctions) {
    bootUp(layers, numberOfLayers, actvFunctions, 0, 0);
}

void bootUp(unsigned int* layers, unsigned int numberOfLayers, byte* actvFunctions, DFLOAT learningRateOfWeights, DFLOAT learningRateOfBiases) {
    _layers = new unsigned int[numberOfLayers];
    for (unsigned int i = 0; i < numberOfLayers; i++) {
        _layers[i] = layers[i];
    }
    _actvFunctions = new byte[numberOfLayers];
    for (unsigned int i = 0; i < numberOfLayers; i++) {
        _actvFunctions[i] = actvFunctions[i];
    }
    _numberOfLayers = numberOfLayers;
    _learningRateOfWeights = learningRateOfWeights;
    _learningRateOfBiases = learningRateOfBiases;
    // _clientName = clientName;

    Serial.println("Booting up...");

    if (!SPIFFS.begin(false)) {
        Serial.println("Error mounting SPIFFS");
        // SPIFFS not able to intialize the partition, cannot load from flash and naither save to it later
        return;
    }

    if (SPIFFS.exists(MODEL_PATH)) {
        if (currentModel != NULL) {
            delete currentModel;
        }
        currentModel = loadModelFromFlash(MODEL_PATH);
    } else {
        // load layer structure (input, hidden_1, hidden_2, hidden_x, output) and activation functions (ReLU, Softmax etc)
        if (currentModel != NULL) {
            delete currentModel;
        }
        /* if you want to follow this way, you need to manage the memory of the arrays yourself since the NN object does not copy the values
        int bsize = 0;
        int wsize = 0;
        for (unsigned int i = 1; i < _numberOfLayers; i++) {
            bsize += _layers[i];
        }
        for (unsigned int i = 1; i < numberOfLayers; i++) {
            wsize += _layers[i] * _layers[i - 1];
        }
        DFLOAT* initialBiases = new DFLOAT[bsize];
        DFLOAT* initialWeights = new DFLOAT[wsize];

        for (int i = 0; i < bsize; i++) {
            // initialBiases[i] = 0.1 + (rand() % 10000) / 100000.0;
            initialBiases[i] = 0.50;
        }
        for (int i = 0; i < wsize; i++) {
            // initialWeights[i] = 0.1 + (rand() % 10000) / 100000.0;
            initialWeights[i] = 0.25;
        }*/
        // currentModel = new NeuralNetwork(_layers, initialWeights, initialBiases, _numberOfLayers, _actvFunctions); // could not figure out a way to set without exploding the gradients
        currentModel = new NeuralNetwork(_layers, _numberOfLayers, _actvFunctions);
        if (_learningRateOfBiases == 0)
            _learningRateOfBiases = currentModel->LearningRateOfBiases;
        if (_learningRateOfWeights == 0)
            _learningRateOfWeights = currentModel->LearningRateOfWeights;
        currentModel->LearningRateOfBiases = _learningRateOfBiases;
        currentModel->LearningRateOfWeights = _learningRateOfWeights;
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
    modelFile.close();
    return result;
}

NeuralNetwork* loadModelFromFlash(const String& file) {
    Serial.println("Loading model from flash...");
    File modelFile = SPIFFS.open(file, "r");
    if (!modelFile) {
        // Error opening file
        Serial.println("Error opening file");
        return NULL;
    } else {
        NeuralNetwork* r = new NeuralNetwork(modelFile);
        modelFile.close();
        return r;
    }
}

model* transformDataToModel(Stream& stream) {
    Serial.println("Transforming data to model...");
    // TODO o tamanho padrão pode ser pequeno demais para caber todos os pesos e biases
    JsonDocument doc;
    
    ReadLoggingStream loggingStream(stream, Serial);
    DeserializationError result = deserializeJson(doc, loggingStream);

    // DeserializationError result = deserializeJson(doc, stream);
    if (result != DeserializationError::Ok) {
        Serial.println(result.code());
        Serial.println("JSON failed to deserialize");
        return NULL;
    }
    const char* precision = doc["precision"];
#if defined(USE_64_BIT_DOUBLE)
    if (strcmp(precision, "double") != 0) {
        // error loading the model, precision missmatch
        return NULL;
    }
#else
    if (strcmp(precision, "float") != 0) {
        // error loading the model, precision missmatch
        return NULL;
    }
#endif
    JsonArray biases = doc["biases"];
    JsonArray weights = doc["weights"];
    DFLOAT* bias = new DFLOAT[biases.size()];
    DFLOAT* weight = new DFLOAT[weights.size()];

    for (int i = 0; i < biases.size(); i++) {
#if defined(USE_64_BIT_DOUBLE)
        // If using double precision and values were serialized as strings
        if (biases[i].is<const char*>()) {
            bias[i] = strtod(biases[i].as<const char*>(), NULL);
        }
        else {
            bias[i] = biases[i].as<DFLOAT>();
        }
#else
        bias[i] = biases[i].as<DFLOAT>();
#endif
        Serial.print(i);
        Serial.print(":");
        Serial.println(bias[i], 8);  // Print with high precision
    }

    for (int i = 0; i < weights.size(); i++) {
#if defined(USE_64_BIT_DOUBLE)
        // If using double precision and values were serialized as strings
        if (weights[i].is<const char*>()) {
            weight[i] = strtod(weights[i].as<const char*>(), NULL);
        }
        else {
            weight[i] = weights[i].as<DFLOAT>();
        }
#else
        weight[i] = weights[i].as<DFLOAT>();
#endif
    }

    Serial.println();
    model *m = new model;
    m->biases = bias;
    m->weights = weight;
    return m;
}

bool trainModelFromOriginalDataset(NeuralNetwork& NN, const String& x_file, const String& y_file) {
    Serial.println("Training model from original dataset...");
    File xFile = SPIFFS.open(x_file, "r");
    File yFile = SPIFFS.open(y_file, "r");

    if (!xFile || !yFile) {
        Serial.println("Error opening file");
        return false;
    }

    // Dynamic buffer allocation
    String xLine, yLine;
    // TODO mover para o heap caso estoure a memória
    DFLOAT x[NN.layers[0]._numberOfInputs], y[NN.layers[NN.numberOflayers - 1]._numberOfOutputs];

    for (int t = 0; t < EPOCHS; t++) {
        Serial.println("Epoch: " + String(t + 1));
        
        // Read from file
        while (xFile.available() && yFile.available()) {
            // Read full lines using String which handles dynamic memory
            xLine = xFile.readStringUntil('\n');
            yLine = yFile.readStringUntil('\n');
            
            if (xLine.length() == 0 || yLine.length() == 0) {
                break;
            }
            
            // Parse x values
            int j = 0;
            int startPos = 0;
            int commaPos = xLine.indexOf(',');
            while (commaPos >= 0 && j < NN.layers[0]._numberOfInputs) {
                String valueStr = xLine.substring(startPos, commaPos);
                #if defined(USE_64_BIT_DOUBLE)
                x[j++] = strtod(valueStr.c_str(), NULL);
                #else
                x[j++] = strtof(valueStr.c_str(), NULL);
                #endif
                startPos = commaPos + 1;
                commaPos = xLine.indexOf(',', startPos);
            }
            // Handle last value
            if (startPos < xLine.length() && j < NN.layers[0]._numberOfInputs) {
                String valueStr = xLine.substring(startPos);
                #if defined(USE_64_BIT_DOUBLE)
                x[j++] = strtod(valueStr.c_str(), NULL);
                #else
                x[j++] = strtof(valueStr.c_str(), NULL);
                #endif
            }
            
            // Parse y values
            int k = 0;
            startPos = 0;
            commaPos = yLine.indexOf(',');
            while (commaPos >= 0 && k < NN.layers[NN.numberOflayers - 1]._numberOfOutputs) {
                String valueStr = yLine.substring(startPos, commaPos);
                #if defined(USE_64_BIT_DOUBLE)
                y[k++] = strtod(valueStr.c_str(), NULL);
                #else
                y[k++] = strtof(valueStr.c_str(), NULL);
                #endif
                startPos = commaPos + 1;
                commaPos = yLine.indexOf(',', startPos);
            }
            // Handle last value
            if (startPos < yLine.length() && k < NN.layers[NN.numberOflayers - 1]._numberOfOutputs) {
                String valueStr = yLine.substring(startPos);
                #if defined(USE_64_BIT_DOUBLE)
                y[k++] = strtod(valueStr.c_str(), NULL);
                #else
                y[k++] = strtof(valueStr.c_str(), NULL);
                #endif
            }
            
            // Train model
            NN.FeedForward(x);
            NN.BackProp(y);
            NN.getMeanSqrdError(1);
        }
        
        yFile.seek(0);
        xFile.seek(0);
    }
    
    xFile.close();
    yFile.close();
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
    }
    else {
        Serial.println("Connecting to wifi...");
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        unsigned long startTime = millis();
        unsigned long timeout = CONNECTION_TIMEOUT; // 30 second timeout
        while (WiFi.status() != WL_CONNECTED && (forever || millis() - startTime < timeout)) {
            delay(500);
        }
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("Wifi connected");
            Serial.println("IP address: " + WiFi.localIP().toString());
            return true;
        }
        Serial.println("Failed to connect to Wifi");
        return false;
    }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
    // TODO pode enfrentar race issues se receber múltiplas mensagens, fora se receber fora de ordem entre outros problemas
    // handle incoming messages

    if (strcmp(topic, MQTT_TOPIC) == 0) {
        if (String((char*)payload).startsWith("INICIO_TRANSMISSAO")) {
            Serial.println("Starting transmission...");
            if (!SPIFFS.exists("/temp")) {
                SPIFFS.mkdir("/temp");
            }
            if (SPIFFS.exists("/temp/new_model.json")) {
                SPIFFS.remove("/temp/new_model.json");
            }
            incomingPayload.clear();
        }
        else if (String((char*)payload).startsWith("FIM_TRANSMISSAO")) {
            model* mm = NULL;
            Serial.println("Ending transmission...");
            if (false) {
                File tempFile = SPIFFS.open("/temp/new_model.json", "r");
                if (!tempFile) {
                    Serial.println("Error opening temp file");
                    return;
                }
                mm = transformDataToModel(tempFile);
                tempFile.close();
            }
            else {
                mm = transformDataToModel(incomingPayload);
            }
            if (mm != NULL && mm->biases != NULL && mm->weights != NULL) {
                if (tempModel != NULL) {
                    delete tempModel;
                }
                if (newModel != NULL) {
                    delete newModel;
                }
                tempModel = mm;
                Serial.println("Model parsed successfully...");
                newModel = new NeuralNetwork(_layers, tempModel->weights, tempModel->biases, _numberOfLayers, _actvFunctions);
                newModel->LearningRateOfBiases = _learningRateOfBiases;
                newModel->LearningRateOfWeights = _learningRateOfWeights;
                trainNewModel = true;
            }
        }
        else {
            // TODO should write into buffer file instead of storing all in memory to avoid issues with big files being received
            Serial.println("Payload received...");
            File ttt = SPIFFS.open("/temp/new_model.json", "a");
            ttt.write(payload, length);
            ttt.close();
            incomingPayload.concat((char*)payload, length);
        }
    }
}

bool connectToServerMQTT(bool forever) {
    if (_client.connected()) {
        Serial.println("Already connected to server MQTT");
        return true;
    }
    else {
        Serial.println("Connecting to server MQTT...");
        unsigned long startTime = millis();
        unsigned long timeout = CONNECTION_TIMEOUT; // 30 second timeout
        while (!_client.connected() && (forever || millis() - startTime < timeout)) {
            // Use a unique client ID with timestamp to avoid connection conflicts
            String clientId = CLIENT_NAME;
            clientId += String(millis());

            if (_client.connect(clientId.c_str())) {
                _client.subscribe(MQTT_TOPIC);
                Serial.println("Connected to server MQTT");
                return true;
            }

            Serial.println("Failed to connect, retrying in 2 seconds...");
            delay(2000);
        }
        Serial.println("Failed to connect to server MQTT");
        return false;
    }
}

void sendModelToNetwork(NeuralNetwork& NN) {

    if (!ensureConnected()) {
        return;
    }

    Serial.println("Sending model to the network...");

    publishWithRetry(MQTT_TOPIC, "INICIO_TRANSMISSAO");

    delay(100);
    // TODO o tamanho padrão pode ser pequeno demais para caber todos os pesos e biases
    JsonDocument doc;

#if defined(USE_64_BIT_DOUBLE)
    doc["precision"] = "double";
#else
    doc["precision"] = "float";
#endif

    doc["biases"] = JsonArray();
    doc["weights"] = JsonArray();

    for (unsigned int n = 0; n < NN.numberOflayers; n++) {
        for (unsigned int i = 0; i < NN.layers[n]._numberOfOutputs; i++) {
#if defined(USE_64_BIT_DOUBLE)
            doc["biases"].add(String(NN.layers[n].bias[i], 16));
#else
            doc["biases"].add(NN.layers[n].bias[i]);
#endif
            for (unsigned int j = 0; j < NN.layers[n]._numberOfInputs; j++) {
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
        size_t bytesToRead = min((size_t)MQTT_MESSAGE_SIZE - 1, bytesRemaining);
        size_t bytes_read = stream.readBytes(buffer, bytesToRead);
        buffer[bytes_read] = '\0';

        publishWithRetry(MQTT_TOPIC, buffer);

        bytesRemaining -= bytes_read;
        currentPosition += bytes_read;

        delay(100);
    }

    Serial.println("Model sent to the network...");
    publishWithRetry(MQTT_TOPIC, "FIM_TRANSMISSAO");

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