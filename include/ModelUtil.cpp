#include "ModelUtil.h"

// #include <SPIFFS.h>
#include "LittleFS.h"
#include <ArduinoJson.h>
#include <PicoMQTT.h>
#include <WiFi.h>

// -------------- Variables

PicoMQTT::Client mqtt(MQTT_BROKER, 1883, CLIENT_NAME, nullptr, nullptr, 5000UL, 30000UL, 10000UL);

unsigned int* _layers;
int _numberOfLayers;
byte* _actvFunctions;
float _learningRateOfWeights;
float _learningRateOfBiases;
model* tempModel;
unsigned long datasetSize = 0;
unsigned long previousTransmit = 0, previousConstruct = 0;
int currentRound = -1;

File xTest, yTest;
// TODO Write into file while receiving the payload to avoid using too much memory.

// -------------- Interface functions

#if DEBUG

unsigned long previousMillis = 0;
multi_heap_info_t info;

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

void printMemory() {
    heap_caps_get_info(&info, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT); // internal RAM, memory capable to store data or to create new task
    D_printf("Heap info: %d bytes free\n", info.total_free_bytes);
    D_printf("Heap info: %d bytes largest free block\n", info.largest_free_block);
    D_printf("Heap info: %d bytes minimum free ever\n", info.minimum_free_bytes);
}

#endif

bool ensureConnected() {
    if (!WiFi.isConnected()) {
        return connectToWifi(false);
    }
    if (!mqtt.connected()) {
        return connectToServerMQTT();
    }
    return true;
}

void bootUp(unsigned int* layers, unsigned int numberOfLayers, byte* actvFunctions) {
    bootUp(layers, numberOfLayers, actvFunctions, 0, 0);
}

void bootUp(unsigned int* layers, unsigned int numberOfLayers, byte* actvFunctions, DFLOAT learningRateOfWeights, DFLOAT learningRateOfBiases) {
    if (!LittleFS.begin(false)) {
        D_println("Error mounting LittleFS");
        // LittleFS not able to intialize the partition, cannot load from flash and naither save to it later
        return;
    }

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

    D_println("Booting up...");

    bool configurationLoaded = loadDeviceConfig();
    bool resumeTraining = false;

    if (configurationLoaded) {
        if (deviceConfig->currentRound != -1 && deviceConfig->currentFederateState != FederateState_NONE) {
            currentRound = deviceConfig->currentRound;
            federateState = deviceConfig->currentFederateState;
            resumeTraining = true;
            if (deviceConfig->newModelState != ModelState_IDLE) {
                // It was not done trainning or it was transmitting or just transmitted before saving
                newModelState = ModelState_IDLE;
            }
        }
    }

    if (LittleFS.exists(MODEL_PATH)) {
        if (currentModel != NULL) {
            delete currentModel;
        }
        currentModel = loadModelFromFlash(MODEL_PATH);
        if (configurationLoaded) {
            // Store the reference to the current model metrics since it's store in the heap
            currentModelMetrics = deviceConfig->currentModelMetrics;
            deviceConfig->currentModelMetrics = NULL;
        }
    }
    else {
        if (resumeTraining) {
            // Code should not be here unless something has gone wrong, but we can still recover be training the model again
        }
        if (currentModel != NULL) {
            delete currentModel;
        }
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
        printMemory();
        currentModelMetrics = trainModelFromOriginalDataset(*currentModel, X_TRAIN_PATH, Y_TRAIN_PATH);
        if (saveModelToFlash(*currentModel, MODEL_PATH)) {
            saveDeviceConfig();
        }
    }

    if (deviceConfig != NULL) {
        delete deviceConfig;
    }

    setupMQTT();

    ensureConnected();

    if (resumeTraining) {
        sendMessageToNetwork(FederateCommand_RESUME);
    }

    D_println("Done booting.");
}

bool saveModelToFlash(NeuralNetwork& NN, const String file) {
    D_println("Saving model to flash...");
    File modelFile = LittleFS.open(file, "w");
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
    File modelFile = LittleFS.open(file, "r");
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
    printMemory();
    // TODO o tamanho padrão pode ser pequeno demais para caber todos os pesos e biases
    unsigned long startTime = millis();
    JsonDocument doc;

    // ! This is triggering the Watchdog from ESP32 since it takes a long time to deserialize.
    DeserializationError result = deserializeJson(doc, stream);
    doc.shrinkToFit();

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
    m->parsingTime = millis() - startTime;
    m->round = -1;
    if (doc["round"].is<int>()) {
        m->round = doc["round"].as<int>();
    }
    printTiming();
    D_println("Transformation complete.");
    return m;
}

multiClassClassifierMetrics* trainModelFromOriginalDataset(NeuralNetwork& NN, const String& x_file, const String& y_file) {
    D_println("Training model from original dataset...");
    printTiming(true);

    unsigned long initTime = millis();

    datasetSize = 0;

    File xFile = LittleFS.open(x_file, "r");
    File yFile = LittleFS.open(y_file, "r");

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
            datasetSize++;

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
            // ! for non binary classification, the metrics are not calculated correctly
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

    metrics->trainingTime = millis() - initTime;
    metrics->epochs = EPOCHS;

    xFile.close();
    yFile.close();
    printTiming();
    D_println("Training complete.");
    return metrics;
}

void processMessages() {
    mqtt.loop();
}

void setupMQTT() {
    D_println("Setting up MQTT...");
    
    connectToWifi(true);
    connectToServerMQTT();

    // TODO Need to handle disconnections properly, when the FL server drops/leaves, when mosquitto dies, when wifi dies

    mqtt.subscribe(MQTT_RECEIVE_COMMANDS_TOPIC, [](const char* topic, Stream& stream) {

        JsonDocument doc;

        // ReadLoggingStream loggingStream(stream, Serial);
        DeserializationError result = deserializeJson(doc, stream);

        // DeserializationError result = deserializeJson(doc, stream);
        if (result != DeserializationError::Ok) {
            D_println(result.code());
            D_println("JSON failed to deserialize");
        } else {
            D_println("Command: " + String(doc["command"].as<const char*>()));
            const char* command = doc["command"];

            if (strcmp(command, "request_model") == 0) {
                sendModelToNetwork(*currentModel, *currentModelMetrics);
                if (federateState == FederateState_TRAINING) {
                    if (newModel != NULL)
                        delete newModel;
                    if (newModelMetrics != NULL)
                        delete newModelMetrics;
                    newModelState = ModelState_IDLE;
                }
            } else if (strcmp(command, "federate_join") == 0) {
                if (federateState == FederateState_NONE) {
                    federateState = FederateState_SUBSCRIBED;
                    saveDeviceConfig();
                    sendMessageToNetwork(FederateCommand_JOIN);
                }
            } else if (strcmp(command, "federate_unsubscribe") == 0) {
                if (federateState != FederateState_NONE) {
                    federateState = FederateState_NONE;
                    currentRound = -1;
                    sendMessageToNetwork(FederateCommand_LEAVE);
                }
            } else if (strcmp(command, "federate_start") == 0) {
                if (federateState == FederateState_SUBSCRIBED) {
                    federateState = FederateState_TRAINING;
                    currentRound = 0;
                    saveDeviceConfig();
                    sendModelToNetwork(*currentModel, *currentModelMetrics);
                }
            } else if (strcmp(command, "federate_end") == 0) {
                if (federateState != FederateState_NONE) {
                    federateState = FederateState_DONE;
                    currentRound = -1;
                }
            } else if (strcmp(command, "federate_stop") == 0) {
                const char* client = doc["client"];
                if (strcmp(client, CLIENT_NAME) == 0) {
                    federateState = FederateState_DONE;
                    currentRound = -1;
                }
            } else if (strcmp(command, "federate_resume") == 0) {
                const char* client = doc["client"];
                if (strcmp(client, CLIENT_NAME) == 0) {
                    D_println("Resuming training...");
                }
            }
        }
    });

    mqtt.subscribe(MQTT_RECEIVE_TOPIC, [](const char* topic, Stream& stream) {
        printMemory();
        if (newModelState != ModelState_IDLE) {
            D_println("Already processing a model");
            return;
        }
        newModelState = ModelState_MODEL_BUSY;
        model* mm = transformDataToModel(stream);
        if (mm != NULL && mm->biases != NULL && mm->weights != NULL) {
            if (tempModel != NULL) {
                delete tempModel;
            }
            if (newModel != NULL) {
                delete newModel;
            }
            if (mm->round >= 0) {
                currentRound = mm->round;
            }
            tempModel = mm;
            D_println("Model parsed successfully...");
            newModel = new NeuralNetwork(_layers, tempModel->weights, tempModel->biases, _numberOfLayers, _actvFunctions);
            newModel->LearningRateOfBiases = _learningRateOfBiases;
            newModel->LearningRateOfWeights = _learningRateOfWeights;
            newModelState = ModelState_READY_TO_TRAIN;
            saveDeviceConfig();
        } else {
            if (mm != NULL) {
                delete mm;
            }
            if (tempModel != NULL) {
                delete tempModel;
            }
            if (newModel != NULL) {
                delete newModel;
            }
            mm = NULL;
            tempModel = NULL;
            newModel = NULL;
            newModelState = ModelState_IDLE;
            D_println("Error parsing model");
        }
    });

    mqtt.subscribe(MQTT_RESUME_TOPIC, [](const char* topic, Stream& stream) {
        newModelState = ModelState_MODEL_BUSY;
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
            newModelState = ModelState_READY_TO_TRAIN;
            federateState = FederateState_TRAINING;
            mqtt.unsubscribe(MQTT_RESUME_TOPIC);
        } else {
            if (mm != NULL) {
                delete mm;
            }
            if (tempModel != NULL) {
                delete tempModel;
            }
            if (newModel != NULL) {
                delete newModel;
            }
            mm = NULL;
            tempModel = NULL;
            newModel = NULL;
            newModelState = ModelState_IDLE;
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

void sendMessageToNetwork(FederateCommand command) {
    if (!ensureConnected()) {
        D_println("Not connected to the network");
        return;
    }

    D_println("Sending command to the network...");

    JsonDocument doc;

    switch (command)
    {
    case FederateCommand_JOIN: {

        doc["command"] = "join";
        doc["client"] = CLIENT_NAME;
        doc["metrics"] = JsonObject();
        doc["metrics"]["accuracy"] = currentModelMetrics->accuracy();
        doc["metrics"]["precision"] = currentModelMetrics->precision();
        doc["metrics"]["recall"] = currentModelMetrics->recall();
        doc["metrics"]["f1Score"] = currentModelMetrics->f1Score();
        doc["metrics"]["meanSqrdError"] = currentModelMetrics->meanSqrdError;
        doc["metrics"]["numberOfClasses"] = currentModelMetrics->numberOfClasses;
        doc["metrics"]["truePositives"] = JsonArray();
        doc["metrics"]["falsePositives"] = JsonArray();
        doc["metrics"]["trueNegatives"] = JsonArray();
        doc["metrics"]["falseNegatives"] = JsonArray();
        for (int i = 0; i < currentModelMetrics->numberOfClasses; i++) {
            doc["metrics"]["truePositives"].add(currentModelMetrics->metrics[i].truePositives);
            doc["metrics"]["falsePositives"].add(currentModelMetrics->metrics[i].falsePositives);
            doc["metrics"]["trueNegatives"].add(currentModelMetrics->metrics[i].trueNegatives);
            doc["metrics"]["falseNegatives"].add(currentModelMetrics->metrics[i].falseNegatives);
        }
        auto publish = mqtt.begin_publish(MQTT_SEND_COMMANDS_TOPIC, measureJson(doc));
        serializeJson(doc, publish);
        publish.send();
        break;
    }
    case FederateCommand_RESUME: {

        D_println("Send command to resume training...");

        doc["command"] = "resume";
        doc["client"] = CLIENT_NAME;
        doc["round"] = currentRound;
        auto publishResume = mqtt.begin_publish(MQTT_SEND_COMMANDS_TOPIC, measureJson(doc));
        serializeJson(doc, publishResume);
        publishResume.send();
        break;
    }
    }
}

void sendModelToNetwork(NeuralNetwork& NN, multiClassClassifierMetrics& metrics) {

    if (!ensureConnected()) {
        D_println("Not connected to the network");
        return;
    }

    D_println("Sending model to the network...");
    printTiming(true);
    unsigned long startTime = millis();

    // TODO the standard size may be too small to fit all weights and biases
    JsonDocument doc;

#if defined(USE_64_BIT_DOUBLE)
    doc["precision"] = "double";
#else
    doc["precision"] = "float";
#endif

    // TODO Migrate this code into a function that persists after the function exits
    doc["biases"] = JsonArray();
    doc["weights"] = JsonArray();
    doc["client"] = CLIENT_NAME;
    doc["metrics"] = JsonObject();
    doc["metrics"]["accuracy"] = metrics.accuracy();
    doc["metrics"]["precision"] = metrics.precision();
    doc["metrics"]["recall"] = metrics.recall();
    doc["metrics"]["f1Score"] = metrics.f1Score();
    doc["metrics"]["meanSqrdError"] = metrics.meanSqrdError;
    doc["metrics"]["numberOfClasses"] = metrics.numberOfClasses;
    doc["metrics"]["truePositives"] = JsonArray();
    doc["metrics"]["falsePositives"] = JsonArray();
    doc["metrics"]["trueNegatives"] = JsonArray();
    doc["metrics"]["falseNegatives"] = JsonArray();
    for (int i = 0; i < metrics.numberOfClasses; i++) {
        doc["metrics"]["truePositives"].add(metrics.metrics[i].truePositives);
        doc["metrics"]["falsePositives"].add(metrics.metrics[i].falsePositives);
        doc["metrics"]["trueNegatives"].add(metrics.metrics[i].trueNegatives);
        doc["metrics"]["falseNegatives"].add(metrics.metrics[i].falseNegatives);
    }

    doc["model"] = JsonArray();
    for (unsigned int n = 0; n < NN.numberOflayers; n++) {
        doc["model"].add(NN.layers[n]._numberOfInputs);
    }
    doc["model"].add(NN.layers[NN.numberOflayers - 1]._numberOfOutputs);
    doc["epochs"] = metrics.epochs;
    doc["datasetSize"] = datasetSize;
    doc["timings"] = JsonObject();
    doc["timings"]["previousTransmit"] = previousTransmit;
    doc["timings"]["previousConstruct"] = previousConstruct;
    doc["timings"]["training"] = metrics.trainingTime;
    doc["timings"]["parsing"] = metrics.parsingTime;

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

    auto publish = mqtt.begin_publish(MQTT_PUBLISH_TOPIC, measureJson(doc));
    serializeJson(doc, publish);
    unsigned long midpoint = millis();
    publish.send();

    unsigned long endTime = millis();
    previousConstruct = midpoint - startTime;
    previousTransmit = endTime - midpoint;

    printTiming();
    D_println(CLIENT_NAME);
    D_println("Model sent to the network...");
}

DFLOAT* predictFromCurrentModel(DFLOAT* x) {
    return currentModel->FeedForward(x);
}

testData* readTestData() {
    if (xTest.name() == nullptr) {
        xTest = LittleFS.open(X_TEST_PATH, "r");
    }

    if (yTest.name() == nullptr) {
        yTest = LittleFS.open(Y_TEST_PATH, "r");
    }

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

bool compareMetrics(multiClassClassifierMetrics* oldMetrics, multiClassClassifierMetrics* newMetrics) {
    if (oldMetrics == NULL || newMetrics == NULL) {
        return false;
    }
    if (newMetrics->accuracy() > oldMetrics->accuracy() ||
        newMetrics->precision() > oldMetrics->precision() ||
        newMetrics->recall() > oldMetrics->recall() ||
        newMetrics->f1Score() > oldMetrics->f1Score()) {
        Serial.println("New model is better than the old one");
        return true;
    }
    return false;
}

void processModel() {
    if (newModelState == ModelState_READY_TO_TRAIN) {
        if (newModelMetrics != NULL) {
            delete newModelMetrics;
        }
        newModelState = ModelState_MODEL_BUSY;
        // ! It was throwing kernel panic due to high cpu usage without releasing the core before increasing the WatchDog timer
        newModelMetrics = trainModelFromOriginalDataset(*newModel, X_TRAIN_PATH, Y_TRAIN_PATH);
        newModelMetrics->parsingTime = tempModel->parsingTime;
        newModelState = ModelState_DONE_TRAINING;
        if (federateState == FederateState_TRAINING) {
            sendModelToNetwork(*newModel, *newModelMetrics);
            if (newModelMetrics != NULL) {
                delete newModelMetrics;
                newModelMetrics = NULL;
            }
            if (newModel != NULL) {
                delete newModel;
                newModel = NULL;
            }
            if (tempModel != NULL) {
                delete tempModel;
                tempModel = NULL;
            }
            newModelState = ModelState_IDLE;
        }
    }
    if (newModelState == ModelState_DONE_TRAINING) {
        if (compareMetrics(currentModelMetrics, newModelMetrics)) {
            delete currentModel;
            currentModel = newModel;
            newModel = NULL;
            newModelState = ModelState_IDLE;
            if (currentModelMetrics != NULL) {
                delete currentModelMetrics;
            }
            currentModelMetrics = newModelMetrics;
            newModelMetrics = NULL;
        }
        else {
            delete newModel;
            newModel = NULL;
            newModelState = ModelState_IDLE;
            if (newModelMetrics != NULL) {
                delete newModelMetrics;
            }
            newModelMetrics = NULL;
        }
        if (federateState == FederateState_DONE) {
            sendModelToNetwork(*currentModel, *currentModelMetrics);
            federateState = FederateState_SUBSCRIBED;
        }
    }
}

bool loadDeviceConfig() {
    D_println("Loading configuration...");
    if (!LittleFS.exists(CONFIGURATION_PATH)) {
        return false;
    }

    File configFile = LittleFS.open(CONFIGURATION_PATH, "r");
    if (!configFile) {
        return false;
    }
    JsonDocument doc;
    // ReadLoggingStream loggingStream(configFile, Serial);
    DeserializationError error = deserializeJson(doc, configFile);
    configFile.close();
    if (error) {
        return false;
    }
    if (deviceConfig != NULL) {
        delete deviceConfig;
    }
    deviceConfig = new DeviceConfig;
    deviceConfig->currentRound = doc["currentRound"] | -1;
    deviceConfig->currentFederateState = static_cast<FederateState>(doc["federateState"] | FederateState_NONE);
    deviceConfig->newModelState = static_cast<ModelState>(doc["modelState"] | ModelState_IDLE);

    deviceConfig->currentModelMetrics = new multiClassClassifierMetrics;
    deviceConfig->currentModelMetrics->numberOfClasses = doc["metrics"]["numberOfClasses"] | 0;
    deviceConfig->currentModelMetrics->epochs = doc["metrics"]["epochs"] | 0;
    deviceConfig->currentModelMetrics->meanSqrdError = doc["metrics"]["meanSqrdError"] | 0;
    deviceConfig->currentModelMetrics->trainingTime = doc["timings"]["training"] | 0;
    deviceConfig->currentModelMetrics->parsingTime = doc["timings"]["parsing"] | 0;
    deviceConfig->currentModelMetrics->metrics = new classClassifierMetricts[deviceConfig->currentModelMetrics->numberOfClasses];
    for (int i = 0; i < deviceConfig->currentModelMetrics->numberOfClasses; i++) {
        deviceConfig->currentModelMetrics->metrics[i].truePositives = doc["metrics"]["truePositives"][i] | 0;
        deviceConfig->currentModelMetrics->metrics[i].falsePositives = doc["metrics"]["falsePositives"][i] | 0;
        deviceConfig->currentModelMetrics->metrics[i].trueNegatives = doc["metrics"]["trueNegatives"][i] | 0;
        deviceConfig->currentModelMetrics->metrics[i].falseNegatives = doc["metrics"]["falseNegatives"][i] | 0;
    }

    if (false) {

        D_println("Current round: " + String(deviceConfig->currentRound));
        D_println("Current federate state: " + String(deviceConfig->currentFederateState));
        D_println("New model state: " + String(deviceConfig->newModelState));
        D_println("Current model metrics: ");
        D_println("Number of classes: " + String(deviceConfig->currentModelMetrics->numberOfClasses));
        D_println("Epochs: " + String(deviceConfig->currentModelMetrics->epochs));
        D_println("Mean squared error: " + String(deviceConfig->currentModelMetrics->meanSqrdError));
        D_println("Training time: " + String(deviceConfig->currentModelMetrics->trainingTime));
        D_println("Parsing time: " + String(deviceConfig->currentModelMetrics->parsingTime));
        for (int i = 0; i < deviceConfig->currentModelMetrics->numberOfClasses; i++) {
            D_println("Class " + String(i) + ": ");
            D_println("True positives: " + String(deviceConfig->currentModelMetrics->metrics[i].truePositives));
            D_println("False positives: " + String(deviceConfig->currentModelMetrics->metrics[i].falsePositives));
            D_println("True negatives: " + String(deviceConfig->currentModelMetrics->metrics[i].trueNegatives));
            D_println("False negatives: " + String(deviceConfig->currentModelMetrics->metrics[i].falseNegatives));
        }
    }

    D_println("Configuration loaded successfully");
    
    return true;
}

bool saveDeviceConfig() {
    File configFile = LittleFS.open(CONFIGURATION_PATH, "w");
    if (!configFile) return false;
    JsonDocument doc;
    
    doc["currentRound"] = currentRound;
    doc["federateState"] = federateState;
    doc["modelState"] = newModelState;
    doc["metrics"] = JsonObject();
    doc["metrics"]["numberOfClasses"] = currentModelMetrics ? currentModelMetrics->numberOfClasses : 0;
    doc["metrics"]["epochs"] = currentModelMetrics ? currentModelMetrics->epochs : 0;
    doc["metrics"]["meanSqrdError"] = currentModelMetrics ? currentModelMetrics->meanSqrdError : 0;
    doc["metrics"]["trainingTime"] = currentModelMetrics ? currentModelMetrics->trainingTime : 0;
    doc["metrics"]["parsingTime"] = currentModelMetrics ? currentModelMetrics->parsingTime : 0;
    for (int i = 0; i < (currentModelMetrics ? currentModelMetrics->numberOfClasses : 0); i++) {
        doc["metrics"]["truePositives"][i] = currentModelMetrics->metrics[i].truePositives;
        doc["metrics"]["falsePositives"][i] = currentModelMetrics->metrics[i].falsePositives;
        doc["metrics"]["trueNegatives"][i] = currentModelMetrics->metrics[i].trueNegatives;
        doc["metrics"]["falseNegatives"][i] = currentModelMetrics->metrics[i].falseNegatives;
    }

    bool result = serializeJson(doc, configFile) > 0;
    configFile.close();

    if (!result) {
        D_println("Failed to save configuration");
    } else {
        D_println("Configuration saved successfully");
    }
    
    return result;
}

// -------------- Unimplemeneted
/*
void receiveModelFromNetwork() {
    File modelFile = LittleFS.open(NEW_MODEL_PATH, "w");
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