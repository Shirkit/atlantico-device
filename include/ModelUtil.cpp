#include "ModelUtil.h"

// #include <SPIFFS.h>
#include "LittleFS.h"
#include <ArduinoJson.h>
#include <PicoMQTT.h>
#include <WiFi.h>

// -------------- Variables

PicoMQTT::Client mqtt(MQTT_BROKER, 1883, "esp", nullptr, nullptr, 5000UL, 30000UL, 10000UL);
// PicoMQTT::Client mqtt(MQTT_BROKER, 1883);

model* tempModel;
unsigned long datasetSize = 0;
unsigned long previousTransmit = 0, previousConstruct = 0;
int currentRound = -1;
bool waitingForMe = false;
bool unsubscribeFromResume = false;
bool sendingMessage = false;

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

void bootUp(bool initBaseModel) {
    if (!LittleFS.begin(false)) {
        D_println("Error mounting LittleFS");
        // LittleFS not able to intialize the partition, cannot load from flash and naither save to it later
        return;
    }

    if (!loadDeviceDefinitions()) {
        return;
    }
    D_println(CLIENT_NAME);

    D_println("Booting up...");

    bool configurationLoaded = loadDeviceConfig();
    bool resumeTraining = false;

    if (configurationLoaded) {
        if (deviceConfig->currentRound != -1 && deviceConfig->currentFederateState != FederateState_NONE) {
            currentRound = deviceConfig->currentRound;
            federateState = deviceConfig->currentFederateState;
            if (deviceConfig->currentFederateState != FederateState_NONE && deviceConfig->loadedFederateModelConfig != nullptr) {
                federateModelConfig = deviceConfig->loadedFederateModelConfig;
                deviceConfig->loadedFederateModelConfig = NULL;
            }
            resumeTraining = true;
            if (deviceConfig->newModelState != ModelState_IDLE) {
                // It was not done trainning or it was transmitting or just transmitted before saving
                newModelState = ModelState_IDLE;
            }
        }
    }

    printMemory();
    fixedMemoryUsage.loadConfig = info.total_free_bytes;

    if (initBaseModel) {
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
            currentModel = new NeuralNetwork(localModelConfig->layers, localModelConfig->numberOfLayers, localModelConfig->actvFunctions);
            currentModel->LearningRateOfBiases = localModelConfig->learningRateOfBiases;
            currentModel->LearningRateOfWeights = localModelConfig->learningRateOfWeights;
            if (currentModelMetrics != NULL) {
                delete currentModelMetrics;
            }
            printMemory();
            currentModelMetrics = trainModelFromOriginalDataset(*currentModel, *localModelConfig, X_TRAIN_PATH, Y_TRAIN_PATH);
            if (saveModelToFlash(*currentModel, MODEL_PATH)) {
                saveDeviceConfig();
            }
        }
    }

    if (deviceConfig != NULL) {
        delete deviceConfig;
    }

    printMemory();
    fixedMemoryUsage.loadAndTrainModel = info.total_free_bytes;

    setupMQTT(resumeTraining);

    ensureConnected();

    if (resumeTraining) {
        sendMessageToNetwork(FederateCommand_RESUME);
    }

    printMemory();
    fixedMemoryUsage.connectionMade = info.total_free_bytes;

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
    IDFLOAT* bias = new IDFLOAT[biases.size()];
    IDFLOAT* weight = new IDFLOAT[weights.size()];

    for (int i = 0; i < biases.size(); i++) {
#if defined(USE_64_BIT_DOUBLE)
        // If using double precision and values were serialized as strings
        if (biases[i].is<const char*>()) {
            bias[i] = strtod(biases[i].as<const char*>(), NULL);
        }
        else {
            bias[i] = biases[i].as<IDFLOAT>();
        }
#else
        bias[i] = biases[i].as<IDFLOAT>();
#endif
    }

    for (int i = 0; i < weights.size(); i++) {
#if defined(USE_64_BIT_DOUBLE)
        // If using double precision and values were serialized as strings
        if (weights[i].is<const char*>()) {
            weight[i] = strtod(weights[i].as<const char*>(), NULL);
        }
        else {
            weight[i] = weights[i].as<IDFLOAT>();
        }
#else
        weight[i] = weights[i].as<IDFLOAT>();
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

multiClassClassifierMetrics* trainModelFromOriginalDataset(NeuralNetwork& NN, ModelConfig& config, const String& x_file, const String& y_file) {
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
    IDFLOAT x[NN.layers[0]._numberOfInputs], y[NN.layers[NN.numberOflayers - 1]._numberOfOutputs];

    multiClassClassifierMetrics* metrics = new multiClassClassifierMetrics;
    metrics->numberOfClasses = NN.layers[NN.numberOflayers - 1]._numberOfOutputs;
    metrics->metrics = new classClassifierMetricts[metrics->numberOfClasses];

    for (int t = 0; t < config.epochs; t++) {
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
            IDFLOAT* predictions = NN.FeedForward(x);
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
    metrics->epochs = config.epochs;

    xFile.close();
    yFile.close();
    printTiming();
    D_println("Training complete.");
    return metrics;
}

void setupFederatedModel() {
    if (newModel != NULL) {
        delete newModel;
    }

    newModel = new NeuralNetwork(federateModelConfig->layers, federateModelConfig->numberOfLayers, federateModelConfig->actvFunctions);
    newModel->LearningRateOfBiases = federateModelConfig->learningRateOfBiases;
    newModel->LearningRateOfWeights = federateModelConfig->learningRateOfWeights;
    newModelState = ModelState_READY_TO_TRAIN;
}

void processMessages() {
    if (!sendingMessage) {
        ensureConnected();
        if (unsubscribeFromResume) {
            mqtt.unsubscribe(MQTT_RESUME_TOPIC);
            String topic = String(MQTT_RAW_RESUME_TOPIC);
            topic.concat("/");
            topic.concat(CLIENT_NAME);
            mqtt.unsubscribe(topic);
            D_println("Unsubscribed from resume topic");
            unsubscribeFromResume = false;
        }
        mqtt.loop();
    }
}

void setupResume() {    
    mqtt.subscribe(MQTT_RESUME_TOPIC, [](const char* topic, Stream& stream) {
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
            tempModel = mm;
            newModel = new NeuralNetwork(federateModelConfig->layers, tempModel->weights, tempModel->biases, federateModelConfig->numberOfLayers, federateModelConfig->actvFunctions);
            newModel->LearningRateOfBiases = federateModelConfig->learningRateOfBiases;
            newModel->LearningRateOfWeights = federateModelConfig->learningRateOfWeights;
            newModelState = ModelState_READY_TO_TRAIN;
            federateState = FederateState_TRAINING;
            unsubscribeFromResume = true;
            D_println("Resume setup done, waiting for training to start...");
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

    String topic = String(MQTT_RAW_RESUME_TOPIC);
    topic.concat("/");
    topic.concat(CLIENT_NAME);

    mqtt.subscribe(topic, [](const char* topic, Stream& stream) {
        if (newModelState != ModelState_IDLE) {
            D_println("Already processing a model");
            return;
        }
        newModelState = ModelState_MODEL_BUSY;

        File file = LittleFS.open(TEMPORARY_NEW_MODEL_PATH, "w+");
        if (!file) {
            D_println("Error opening file for writing");
            return;
        }
        while (stream.available()) {
            file.write(stream.read());
        }
        file.seek(0);

        if (tempModel != NULL) {
            delete tempModel;
        }
        if (newModel != NULL) {
            delete newModel;
        }

        newModel = new NeuralNetwork(localModelConfig->layers, localModelConfig->numberOfLayers, localModelConfig->actvFunctions);
        newModel->LearningRateOfBiases = localModelConfig->learningRateOfBiases;
        newModel->LearningRateOfWeights = localModelConfig->learningRateOfWeights;
        
        if (newModel->load(file)) {
            newModelState = ModelState_READY_TO_TRAIN;
            federateState = FederateState_TRAINING;
            unsubscribeFromResume = true;
            D_println("Resume setup done, waiting for training to start...");
        } else {
            D_println("Error loading model from file");

            if (tempModel != NULL) {
                delete tempModel;
            }
            if (newModel != NULL) {
                delete newModel;
            }
            tempModel = NULL;
            newModel = NULL;
            newModelState = ModelState_IDLE;
            D_println("Error parsing model");
        }
    });
}

void setupMQTT(bool resume) {
    D_println("Setting up MQTT...");

    // WiFi.setTxPower(WIFI_POWER_MINUS_1dBm);

    D_println("power set");
    
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
                    saveDeviceConfig();
                }
            } else if (strcmp(command, "federate_start") == 0) {
                if (federateState == FederateState_SUBSCRIBED) {
                    if (doc["config"].is<JsonObject>()) {
                        unsigned int* federateLayers = new unsigned int[doc["config"]["layers"].size()];
                        for (int i = 0; i < doc["config"]["layers"].size(); i++) {
                            federateLayers[i] = doc["config"]["layers"][i].as<unsigned int>();
                        }
                        byte* federateActvFunctions = new byte[doc["config"]["actvFunctions"].size()];
                        for (int i = 0; i < doc["config"]["actvFunctions"].size(); i++) {
                            federateActvFunctions[i] = doc["config"]["actvFunctions"][i].as<byte>();
                        }
                        federateModelConfig = new ModelConfig(federateLayers, doc["config"]["layers"].size(), federateActvFunctions);
                        if (doc["randomSeed"].is<unsigned long>()) {
                            federateModelConfig->randomSeed = doc["randomSeed"].as<unsigned long>();
                            randomSeed(federateModelConfig->randomSeed);
                        }
                        if (doc["config"]["epochs"].is<unsigned int>()) {
                            federateModelConfig->epochs = doc["config"]["epochs"].as<unsigned int>();
                        }
                        if (doc["config"]["learningRateOfWeights"].is<IDFLOAT>()) {
                            federateModelConfig->learningRateOfWeights = doc["config"]["learningRateOfWeights"].as<IDFLOAT>();
                        }
                        if (doc["config"]["learningRateOfBiases"].is<IDFLOAT>()) {
                            federateModelConfig->learningRateOfBiases = doc["config"]["learningRateOfBiases"].as<IDFLOAT>();
                        }
                        federateState = FederateState_TRAINING;
                        currentRound = 0;
                        setupFederatedModel();
                        saveDeviceConfig();
                    }
                }
            } else if (strcmp(command, "federate_end") == 0) {
                if (federateState != FederateState_NONE) {
                    federateState = FederateState_DONE;
                    currentRound = -1;
                    saveDeviceConfig();
                }
            } else if (strcmp(command, "federate_stop") == 0) {
                const char* client = doc["client"];
                if (strcmp(client, CLIENT_NAME) == 0) {
                    federateState = FederateState_DONE;
                    currentRound = -1;
                    saveDeviceConfig();
                }
            } else if (strcmp(command, "federate_resume") == 0) {
                const char* client = doc["client"];
                if (strcmp(client, CLIENT_NAME) == 0) {
                    D_println("Resuming training...");
                    if (currentRound == 0) {
                        setupFederatedModel();
                        D_println("Setup done");
                    }
                }
            } else if (strcmp(command, "federate_waiting") == 0) {
                JsonArray clients = doc["clients"];
                for (int i = 0; i < clients.size(); i++) {
                    if (strcmp(clients[i].as<const char*>(), CLIENT_NAME) == 0) {
                        if (currentRound == doc["round"].as<int>() && newModelState == ModelState_IDLE) {
                            if (waitingForMe) {
                                // TODO if we do not discard the sent/built newModel we could try to resend it, need a refactor for that
                                setupResume();
                                sendMessageToNetwork(FederateCommand_RESUME);
                                waitingForMe = false;
                            } else {
                                waitingForMe = true;
                            }
                        } else {
                            sendMessageToNetwork(FederateCommand_ALIVE);
                            break;
                        }
                    }
                }
            } else if (strcmp(command, "federate_alive") == 0) {
                sendMessageToNetwork(FederateCommand_ALIVE);
            }
        }
    });

    mqtt.subscribe(MQTT_RAW_RECEIVE_TOPIC, [](const char* topic, Stream& stream) {
        unsigned long startTime = millis();
        printMemory();
        roundMemoryUsage.messageReceived = info.total_free_bytes;
        if (newModelState != ModelState_IDLE) {
            D_println("Already processing a model");
            return;
        }
        newModelState = ModelState_MODEL_BUSY;
        File file = LittleFS.open(TEMPORARY_NEW_MODEL_PATH, "w+");
        if (!file) {
            D_println("Error opening file for writing");
            return;
        }
        while (stream.available()) {
            file.write(stream.read());
        }
        file.seek(0);

        if (federateState == FederateState_NONE) {
            newModel = new NeuralNetwork(localModelConfig->layers, localModelConfig->numberOfLayers, localModelConfig->actvFunctions);
            newModel->LearningRateOfBiases = localModelConfig->learningRateOfBiases;
            newModel->LearningRateOfWeights = localModelConfig->learningRateOfWeights;
        } else {
            newModel = new NeuralNetwork(federateModelConfig->layers, federateModelConfig->numberOfLayers, federateModelConfig->actvFunctions);
            newModel->LearningRateOfBiases = federateModelConfig->learningRateOfBiases;
            newModel->LearningRateOfWeights = federateModelConfig->learningRateOfWeights;
        }

        if (newModel->load(file)) {
            D_println("Model loaded successfully from file");
            if (tempModel != NULL) {
                delete tempModel;
            }
            tempModel = new model;
            tempModel->parsingTime = millis() - startTime;
            currentRound++;

            newModelState = ModelState_READY_TO_TRAIN;
            saveDeviceConfig();
            D_println("New model ready to train");

        } else {
            D_println("Error loading model from file");

            if (tempModel != NULL) {
                delete tempModel;
            }
            if (newModel != NULL) {
                delete newModel;
            }
            tempModel = NULL;
            newModel = NULL;
            newModelState = ModelState_IDLE;
            D_println("Error parsing model");
        }
    });

    mqtt.subscribe(MQTT_RECEIVE_TOPIC, [](const char* topic, Stream& stream) {
        printMemory();
        roundMemoryUsage.messageReceived = info.total_free_bytes;
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
            D_println("Model parsed successfully from subscribe...");
            if (federateState == FederateState_NONE) {
                newModel = new NeuralNetwork(localModelConfig->layers, tempModel->weights, tempModel->biases, localModelConfig->numberOfLayers, localModelConfig->actvFunctions);
                newModel->LearningRateOfBiases = localModelConfig->learningRateOfBiases;
                newModel->LearningRateOfWeights = localModelConfig->learningRateOfWeights;
            } else {
                newModel = new NeuralNetwork(federateModelConfig->layers, tempModel->weights, tempModel->biases, federateModelConfig->numberOfLayers, federateModelConfig->actvFunctions);
                newModel->LearningRateOfBiases = federateModelConfig->learningRateOfBiases;
                newModel->LearningRateOfWeights = federateModelConfig->learningRateOfWeights;
            }
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

    if (resume) {
        setupResume();
    }
}

bool connectToWifi(bool forever) {
    WiFi.config(IPAddress(192, 168, 15, 40 + String(CLIENT_NAME).substring(3).toInt()), IPAddress(192, 168, 15, 1), IPAddress(255, 255, 255, 0));
    D_println("wifi ip set");
    if (WiFi.status() == WL_CONNECTED) {
        D_println("Already connected to Wifi");
        return true;
    }
    else {
        D_println("Connecting to wifi...");
        // delay(500);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        unsigned long startTime = millis();
        unsigned long timeout = CONNECTION_TIMEOUT; // 30 second timeout
        while (WiFi.status() != WL_CONNECTED && (forever || millis() - startTime < timeout)) {
            switch(WiFi.status()) {
                case WL_NO_SSID_AVAIL:
                    D_println("No SSID available");
                    break;
                case WL_CONNECT_FAILED:
                    D_println("Connection failed");
                    break;
                case WL_DISCONNECTED:
                    D_println("Disconnected from Wifi");
                    // delay(500);
                    // D_println("persistent");
                    // WiFi.persistent(false);
                    // delay(500);
                    // D_println("force disconnect");
                    // WiFi.disconnect(true, true);
                    // delay(500);
                    // D_println("rebegin wifi");
                    // WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
                    // delay(500);
                    break;
                case WL_IDLE_STATUS:
                    D_println("Wifi idle status");
                    // delay(500);
                    // WiFi.persistent(false);
                    // delay(500);
                    // WiFi.disconnect(true, true);
                    // delay(500);
                    // WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
                    // delay(500);
                    break;
                case WL_CONNECTED:
                    D_println("Wifi connected");
                    break;
                case WL_CONNECTION_LOST:
                    D_println("Connection lost");
                    break;
                case WL_NO_SHIELD:
                    D_println("No shield available");
                    break;
                case WL_SCAN_COMPLETED:
                    D_println("Scan completed");
                    break;
                default:
                    break;
            }
            delay(500);
        }
        if (WiFi.status() == WL_CONNECTED) {
            D_println("Wifi connected");
            D_println("IP address: " + WiFi.localIP().toString());
            delay(500);
            return true;
        }
        D_println("Failed to connect to Wifi");
        return false;
    }
}

bool connectToServerMQTT() {
    return mqtt.connect(MQTT_BROKER, 1883, CLIENT_NAME);
}

const char* modelStateToString(ModelState state) {
    switch (state) {
        case ModelState_IDLE:
            return "idle";
        case ModelState_READY_TO_TRAIN:
            return "ready_to_train";
        case ModelState_MODEL_BUSY:
            return "model_busy";
        case ModelState_WAITING_DOWNLOAD:
            return "waiting_download";
        case ModelState_DONE_TRAINING:
            return "done_training";
        default:
            return "unknown";
    }
}

void sendMessageToNetwork(FederateCommand command) {
    if (!ensureConnected()) {
        D_println("Not connected to the network");
        return;
    }

    if (sendingMessage) {
        return;
    }
    sendingMessage = true;

    D_println("Sending command to the network...");

    JsonDocument doc;

    switch (command)
    {
    case FederateCommand_JOIN: {

        doc["command"] = "join";
        doc["client"] = CLIENT_NAME;
        /*doc["metrics"] = JsonObject();
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
        }*/
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
    case FederateCommand_ALIVE: {        
        doc["command"] = "alive";
        doc["client"] = CLIENT_NAME;
        doc["round"] = currentRound;
        doc["newModelState"] = modelStateToString(newModelState);
        auto publishAlive = mqtt.begin_publish(MQTT_SEND_COMMANDS_TOPIC, measureJson(doc));
        serializeJson(doc, publishAlive);
        publishAlive.send();
        break;
    }
    }
    sendingMessage = false;
}

void sendModelToNetwork(NeuralNetwork& NN, multiClassClassifierMetrics& metrics) {
    // ! PicoMQTT can only handle send one message at a time, so we do a semaphore to prevent other messages from being sent at the same time
    while (sendingMessage) delay(10);
    sendingMessage = true;

    if (!ensureConnected()) {
        D_println("Not connected to the network");
        return;
    }

    D_println("Sending model to the network...");
    printMemory();
    roundMemoryUsage.beforeSend = info.total_free_bytes;
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
    // doc["biases"] = JsonArray();
    // doc["weights"] = JsonArray();
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

    doc["memory"] = JsonObject();
    doc["memory"]["fixed"] = JsonObject();
    doc["memory"]["fixed"]["onBoot"] = fixedMemoryUsage.onBoot;
    doc["memory"]["fixed"]["loadConfig"] = fixedMemoryUsage.loadConfig;
    doc["memory"]["fixed"]["loadAndTrainModel"] = fixedMemoryUsage.loadAndTrainModel;
    doc["memory"]["fixed"]["connectionMade"] = fixedMemoryUsage.connectionMade;
    doc["memory"]["fixed"]["afterFullSetup"] = fixedMemoryUsage.afterFullSetup;
    doc["memory"]["fixed"]["minFreeHeapAfterSetup"] = fixedMemoryUsage.minFreeHeapAfterSetup;

    doc["memory"]["round"] = JsonObject();
    doc["memory"]["round"]["messageReceived"] = roundMemoryUsage.messageReceived;
    doc["memory"]["round"]["beforeTrain"] = roundMemoryUsage.beforeTrain;
    doc["memory"]["round"]["afterTrain"] = roundMemoryUsage.afterTrain;

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

    File modelFile = LittleFS.open(TEMPORARY_NEW_MODEL_PATH, "w");
    NN.save(modelFile);
    
    printMemory();
    roundMemoryUsage.beforeSend = info.total_free_bytes;
    roundMemoryUsage.minimumFree = info.minimum_free_bytes;

    doc["memory"]["round"]["beforeSend"] = roundMemoryUsage.beforeSend;
    doc["memory"]["round"]["minimumFree"] = roundMemoryUsage.minimumFree;
        
    String topic = String(MQTT_RAW_PUBLISH_TOPIC);
    topic.concat("/");
    topic.concat(CLIENT_NAME);
    modelFile = LittleFS.open(TEMPORARY_NEW_MODEL_PATH, "r");
    size_t size = modelFile.available();
    D_println("Model serialized to file");
    D_println("Topic: " + topic);
    char* buffer = new char[1024];
    uint8_t* buff = (uint8_t*)buffer;
    size_t bytesRead = modelFile.readBytes(buffer, 1024);
    auto publish = mqtt.begin_publish(topic, size);
    while (bytesRead > 0) {
        publish.write(buff, bytesRead);
        bytesRead = modelFile.readBytes(buffer, 1024);
    }
    publish.send();
    delete[] buffer;
    modelFile.close();

    auto publish2 = mqtt.begin_publish(MQTT_PUBLISH_TOPIC, measureJson(doc));
    serializeJson(doc, publish2);
    unsigned long midpoint = millis();
    publish2.send();

    unsigned long endTime = millis();
    previousConstruct = midpoint - startTime;
    previousTransmit = endTime - midpoint;

    printTiming();
    D_println(CLIENT_NAME);
    D_println("Model sent to the network...");
    sendingMessage = false;
}

DFLOAT* predictFromCurrentModel(DFLOAT* x) {
    return currentModel->FeedForward(x);
}

testData* readTestData(ModelConfig* modelConfig) {
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

    // TODO is there a better way to do this? do we need to allocate on the heap? can we use a fixed size array and attach on the struct without the heap?
    IDFLOAT* x = new IDFLOAT[modelConfig->layers[0]], * y = new IDFLOAT[modelConfig->layers[modelConfig->numberOfLayers - 1]];

    while (commaPos >= 0 && j < modelConfig->layers[0]) {
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
    if (startPos < xLine.length() && j < modelConfig->layers[0]) {
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
    while (commaPos >= 0 && k < modelConfig->layers[modelConfig->numberOfLayers - 1]) {
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
    if (startPos < yLine.length() && k < modelConfig->layers[modelConfig->numberOfLayers - 1]) {
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
        printMemory();
        roundMemoryUsage.beforeTrain = info.total_free_bytes;
        if (newModelMetrics != NULL) {
            delete newModelMetrics;
        }
        newModelState = ModelState_MODEL_BUSY;
        // ! It was throwing kernel panic due to high cpu usage without releasing the core before increasing the WatchDog timer
        newModelMetrics = trainModelFromOriginalDataset(*newModel, *localModelConfig, X_TRAIN_PATH, Y_TRAIN_PATH);
        if (tempModel != NULL) {
            newModelMetrics->parsingTime = tempModel->parsingTime;
        }
        newModelState = ModelState_DONE_TRAINING;
        printMemory();
        roundMemoryUsage.afterTrain = info.total_free_bytes;
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
    if (newModelState == ModelState_DONE_TRAINING && currentModel != NULL) {
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
            federateState = FederateState_NONE;
            currentRound = -1;
            saveDeviceConfig();
        }
    }
}

bool loadDeviceDefinitions() {
    if (!LittleFS.exists(DEVICE_DEFINITION_PATH)) {
        return false;
    }
    File definitionsFile = LittleFS.open(DEVICE_DEFINITION_PATH, "r");
    if (!definitionsFile) {
        return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, definitionsFile);
    definitionsFile.close();
    if (error) {
        return false;
    }


    // Copy the value immediately while doc is still valid
    const char* clientValue = doc["client"] | "esp";
    if (CLIENT_NAME != nullptr) {
        delete[] CLIENT_NAME;
    }
    CLIENT_NAME = new char[strlen(clientValue) + 1];
    strcpy(CLIENT_NAME, clientValue);

    return true;
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
    if (doc["federateModelConfig"].is<JsonObject>()) {
        JsonObject federateModelConfigObj = doc["federateModelConfig"];
        unsigned int* layers = new unsigned int[federateModelConfigObj["layers"].size()];
        for (int i = 0; i < federateModelConfigObj["layers"].size(); i++) {
            layers[i] = federateModelConfigObj["layers"][i].as<unsigned int>();
        }
        byte* actvFunctions = new byte[federateModelConfigObj["actvFunctions"].size()];
        for (int i = 0; i < federateModelConfigObj["actvFunctions"].size(); i++) {
            actvFunctions[i] = federateModelConfigObj["actvFunctions"][i].as<byte>();
        }
        deviceConfig->loadedFederateModelConfig = new ModelConfig(layers, federateModelConfigObj["layers"].size(), actvFunctions, 
                                                            federateModelConfigObj["learningRateOfWeights"].as<IDFLOAT>(), 
                                                            federateModelConfigObj["learningRateOfBiases"].as<IDFLOAT>());
        deviceConfig->loadedFederateModelConfig->numberOfLayers = federateModelConfigObj["numberOfLayers"] | federateModelConfigObj["layers"].size() - 1;
        deviceConfig->loadedFederateModelConfig->epochs = federateModelConfigObj["epochs"] | 1;
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
    if (federateModelConfig) {
        doc["federateModelConfig"] = JsonObject();
        doc["federateModelConfig"]["layers"] = JsonArray();
        for (int i = 0; i < federateModelConfig->numberOfLayers; i++) {
            doc["federateModelConfig"]["layers"].add(federateModelConfig->layers[i]);
        }
        doc["federateModelConfig"]["actvFunctions"] = JsonArray();
        for (int i = 0; i < federateModelConfig->numberOfLayers-1; i++) {
            doc["federateModelConfig"]["actvFunctions"].add(federateModelConfig->actvFunctions[i]);
        }
        doc["federateModelConfig"]["learningRateOfWeights"] = federateModelConfig->learningRateOfWeights;
        doc["federateModelConfig"]["learningRateOfBiases"] = federateModelConfig->learningRateOfBiases;
        doc["federateModelConfig"]["numberOfLayers"] = federateModelConfig->numberOfLayers;
        doc["federateModelConfig"]["epochs"] = federateModelConfig->epochs;
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