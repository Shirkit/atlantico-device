#include "ModelUtil.h"

#include <SPIFFS.h>
#include <ArduinoJson.h>
// #include <PubSubClient.h>
#include <PicoMQTT.h>
#include <WiFi.h>

// -------------- Variables

WiFiClient __espClient;
PicoMQTT::Client mqtt(MQTT_BROKER, 1883);

// StreamString incomingPayload = StreamString();
unsigned int* _layers;
int _numberOfLayers;
byte* _actvFunctions;
DFLOAT _learningRateOfWeights;
DFLOAT _learningRateOfBiases;
model* tempModel;

File xTest, yTest;
// TODO Write into file while receiving the payload to avoid using too much memory.
// String _clientName;

// -------------- Interface functions

// #if DEBUG

unsigned long previousMillis = 0;

void printTiming(bool doReset = false) {
    if (doReset || previousMillis == 0) {
        previousMillis = millis();
    }
    else {
        unsigned long currentMillis = millis();
        D_print("Time elapsed: ");
        D_print(((float)currentMillis - previousMillis) / 1000.0, 1);
        D_println(" seconds (" + String(currentMillis - previousMillis) + " ms)");
        previousMillis = 0;
    }
}

// #endif

bool ensureConnected() {
    if (!WiFi.isConnected()) {
        return connectToWifi(false);
    }
    if (!mqtt.connected()) {
        return connectToServerMQTT();
    }
    return true;
}

/*bool publishWithRetry(const char* topic, const char* payload, int retries = 3) {
    for (int i = 0; i < retries; i++) {
        if (_client.publish(topic, payload)) {
            return true;
        }
        delay(100);
    }
    return false;
  }*/

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

    D_println("Booting up...");

    if (!SPIFFS.begin(false)) {
        D_println("Error mounting SPIFFS");
        // SPIFFS not able to intialize the partition, cannot load from flash and naither save to it later
        return;
    }

    if (SPIFFS.exists(MODEL_PATH)) {
        if (currentModel != NULL) {
            delete currentModel;
        }
        currentModel = loadModelFromFlash(MODEL_PATH);
    }
    else {
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
        if (currentModelMetrics != NULL) {
            delete currentModelMetrics;
        }
        currentModelMetrics = trainModelFromOriginalDataset(*currentModel, X_TRAIN_PATH, Y_TRAIN_PATH);
    }

    xTest = SPIFFS.open(X_TEST_PATH, "r");
    yTest = SPIFFS.open(Y_TEST_PATH, "r");

    setupMQTT();

    D_println("Done booting.");
}

bool saveModelToFlash(NeuralNetwork& NN, const String file) {
    D_println("Saving model to flash...");
    File modelFile = SPIFFS.open(file, "w");
    bool result;
    if (!modelFile) {
        // Error opening file
        result = false;
    }
    else {
        result = NN.save(modelFile);
    }
    D_println("Result: " + String(result));
    modelFile.close();
    return result;
}

NeuralNetwork* loadModelFromFlash(const String& file) {
    D_println("Loading model from flash...");
    File modelFile = SPIFFS.open(file, "r");
    if (!modelFile) {
        // Error opening file
        D_println("Error opening file");
        return NULL;
    }
    else {
        NeuralNetwork* r = new NeuralNetwork(modelFile);
        modelFile.close();
        D_println("Model loaded successfully");
        return r;
    }
}

model* transformDataToModel(Stream& stream) {
    D_println("Transforming data to model...");
    printTiming(true);
    // TODO o tamanho padrão pode ser pequeno demais para caber todos os pesos e biases
    JsonDocument doc;

    D_println(stream.available());

    // ReadLoggingStream loggingStream(stream, Serial);
    DeserializationError result = deserializeJson(doc, stream);

    // DeserializationError result = deserializeJson(doc, stream);
    if (result != DeserializationError::Ok) {
        D_println(result.code());
        D_println("JSON failed to deserialize");
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

    model* m = new model;
    m->biases = bias;
    m->weights = weight;
    printTiming();
    D_println("Transformation complete.");
    return m;
}

multiClassClassifierMetrics* trainModelFromOriginalDataset(NeuralNetwork& NN, const String& x_file, const String& y_file) {
    D_println("Training model from original dataset...");
    printTiming(true);
    File xFile = SPIFFS.open(x_file, "r");
    File yFile = SPIFFS.open(y_file, "r");

    if (!xFile || !yFile) {
        D_println("Error opening file");
        return NULL;
    }

    // Dynamic buffer allocation
    String xLine, yLine;
    // TODO mover para o heap caso estoure a memória
    DFLOAT x[NN.layers[0]._numberOfInputs], y[NN.layers[NN.numberOflayers - 1]._numberOfOutputs];

    multiClassClassifierMetrics* metrics = new multiClassClassifierMetrics;
    metrics->numberOfClasses = NN.layers[NN.numberOflayers - 1]._numberOfOutputs;
    metrics->metrics = new classClassifierMetricts[metrics->numberOfClasses];

    for (int t = 0; t < EPOCHS; t++) {
        D_println("Epoch: " + String(t + 1));

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
            DFLOAT* predictions = NN.FeedForward(x);
            NN.BackProp(y);
            metrics->meanSqrdError = NN.getMeanSqrdError(1);

            // Calculate metrics
            for (int i = 0; i < metrics->numberOfClasses; i++) {
                if (y[i] == 1) {
                    if (predictions[i] >= 0.5) {
                        metrics->metrics[i].truePositives++;
                    }
                    else {
                        metrics->metrics[i].falseNegatives++;
                    }
                }
                else {
                    if (predictions[i] >= 0.5) {
                        metrics->metrics[i].falsePositives++;
                    }
                    else {
                        metrics->metrics[i].trueNegatives++;
                    }
                }
            }


        }

        yFile.seek(0);
        xFile.seek(0);
    }

    xFile.close();
    yFile.close();
    printTiming();
    D_println("Training complete.");
    return metrics;
}

void processMessages() {
    //_client.loop();
    mqtt.loop();
}


// -------------- Local functions

void setupMQTT() {
    D_println("Setting up MQTT...");
    connectToWifi(true);
    mqtt.client_id = CLIENT_NAME;
    mqtt.host = MQTT_BROKER;
    mqtt.port = 1883;
    connectToServerMQTT();

    mqtt.subscribe(MQTT_TOPIC, [](const char * topic, Stream & stream) {
        model* mm = transformDataToModel(stream);
        if (mm != NULL && mm->biases != NULL && mm->weights != NULL) {
            if (tempModel != NULL) {
                delete tempModel;
            }
            if (newModel != NULL) {
                delete newModel;
            }
            tempModel = mm;
            D_println("Model parsed successfully...");
            newModel = new NeuralNetwork(_layers, tempModel->weights, tempModel->biases, _numberOfLayers, _actvFunctions);
            newModel->LearningRateOfBiases = _learningRateOfBiases;
            newModel->LearningRateOfWeights = _learningRateOfWeights;
            trainNewModel = true;
        } 
        else {
            D_println("Error parsing model");
        }
    });
}

bool connectToWifi(bool forever) {
    if (WiFi.status() == WL_CONNECTED) {
        D_println("Already connected to Wifi");
        return true;
    }
    else {
        D_println("Connecting to wifi...");
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        unsigned long startTime = millis();
        unsigned long timeout = CONNECTION_TIMEOUT; // 30 second timeout
        while (WiFi.status() != WL_CONNECTED && (forever || millis() - startTime < timeout)) {
            delay(500);
        }
        if (WiFi.status() == WL_CONNECTED) {
            D_println("Wifi connected");
            D_println("IP address: " + WiFi.localIP().toString());
            return true;
        }
        D_println("Failed to connect to Wifi");
        return false;
    }
}

bool connectToServerMQTT() {
    return mqtt.connect(MQTT_BROKER, 1883, CLIENT_NAME);
}

void sendModelToNetwork(NeuralNetwork& NN) {

    if (!ensureConnected()) {
        return;
    }

    D_println("Sending model to the network...");
    printTiming(true);

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

    auto publish = mqtt.begin_publish(MQTT_TOPIC, measureJson(doc));
    serializeJson(doc, publish);
    publish.send();

    printTiming();
    D_println("Model sent to the network...");

    //    D_println(stream);
}

DFLOAT* predictFromCurrentModel(DFLOAT* x) {
    return currentModel->FeedForward(x);
}

testData* readTestData() {
    if (!xTest) {
        D_println("Error opening file");
        return NULL;
    }
    if (!yTest) {
        D_println("Error opening file");
        return NULL;
    }

    if (!xTest.available() || !yTest.available()) {
        xTest.seek(0);
        yTest.seek(0);
    }

    String xLine = xTest.readStringUntil('\n');
    String yLine = yTest.readStringUntil('\n');

    if (xLine.length() == 0 || yLine.length() == 0) {
        xTest.seek(0);
        yTest.seek(0);
        xLine = xTest.readStringUntil('\n');
        yLine = yTest.readStringUntil('\n');
        if (xLine.length() == 0 || yLine.length() == 0) {
            // can't recover from this error
            D_println("Error reading test file");
            return NULL;
        }
    }

    int j = 0;
    int startPos = 0;
    int commaPos = xLine.indexOf(',');

    // is there a better way to do this? do we need to allocate on the heap? can we use a fixed size array and attach on the struct without the heap?
    DFLOAT* x = new DFLOAT[_layers[0]], * y = new DFLOAT[_layers[_numberOfLayers - 1]];

    while (commaPos >= 0 && j < _layers[0]) {
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
    if (startPos < xLine.length() && j < _layers[0]) {
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
    while (commaPos >= 0 && k < _layers[_numberOfLayers - 1]) {
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
    if (startPos < yLine.length() && k < _layers[_numberOfLayers - 1]) {
        String valueStr = yLine.substring(startPos);
        #if defined(USE_64_BIT_DOUBLE)
        y[k++] = strtod(valueStr.c_str(), NULL);
        #else
        y[k++] = strtof(valueStr.c_str(), NULL);
        #endif
    }

    // is there a better way to do this? do we need to allocate on the heap? can we use a fixed size array and attach on the struct without the heap?
    testData* td = new testData;
    td->x = x;
    td->y = y;
    return td;

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

void readDataFromSensors() {

}

void writeDataToDatabase() {

}

*/