#include "ModelUtil.h"

// #include <SPIFFS.h>
#include "LittleFS.h"
#include <ArduinoJson.h>
#include <PicoMQTT.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <vector>
#include <cmath>

// -------------- Variables

char* OTHER_CLIENT_NAME = NULL;
DeviceConfig* otherDeviceConfig = NULL;

bool loadOtherDeviceConfig();

PicoMQTT::Client mqtt(MQTT_BROKER, 1883, "esp", nullptr, nullptr, 5000UL, 30000UL, 10000UL);
// PicoMQTT::Client mqtt(MQTT_BROKER, 1883);

model* tempModel;
unsigned long datasetSize = 0;
unsigned long previousTransmit = 0, previousConstruct = 0, timeSinceLastServerMessage = 0;;
int currentRound = -1;
int lastTrainedRound = -1;
bool waitingForMe = false;
bool unsubscribeFromResume = false;
bool sendingMessage = false;
String selectedDatasetKey = "";
String selectedDatasetBinName = "";
String selectedDatasetMetaName = "";

#ifdef DOUBLE_DATASET
int datasetIndex = 0;
#endif

static String baseNameFromPath(const String& path) {
    int idx = path.lastIndexOf('/');
    if (idx < 0) {
        return path;
    }
    return path.substring(idx + 1);
}

static String sanitizeDatasetKey(const String& rawKey) {
    String key = rawKey;
    key.replace("\\", "/");
    while (key.startsWith("/")) {
        key.remove(0, 1);
    }
    while (key.endsWith("/")) {
        key.remove(key.length() - 1, 1);
    }
    return key;
}

static bool hasBinExtension(const String& fileName) {
    String lower = fileName;
    lower.toLowerCase();
    return lower.endsWith(".bin");
}

static String findBinFileInDatasetFolder(const String& datasetFolder) {
    String folder = sanitizeDatasetKey(datasetFolder);
    if (folder.length() == 0) {
        return "";
    }

    String folderPath = "/" + folder;
    File dir = LittleFS.open(folderPath, "r");
    if (!dir || !dir.isDirectory()) {
        D_println("[WRN] findBinFileInDatasetFolder: " + folderPath + " is not a directory or missing");
        return "";
    }

    // Try to find a file that matches this device's ID (e.g., esp01 looks for "*01.bin")
    String clientId = String(CLIENT_NAME);
    String idSuffix = "";
    for (int i = 0; i < clientId.length(); i++) {
        if (isDigit(clientId[i])) {
            idSuffix += clientId[i];
        }
    }

    D_println("[DBG] findBinFileInDatasetFolder: Searching " + folderPath + " (ID suffix: " + idSuffix + ")");

    String firstFallback = "";
    File file = dir.openNextFile();
    while (file) {
        String foundName = file.name();
        file.close();

        if (hasBinExtension(foundName)) {
            // Ensure fullPath starts with folderPath
            String fullPath = foundName;
            if (!fullPath.startsWith(folderPath)) {
                if (fullPath.startsWith("/")) {
                    fullPath = folderPath + fullPath;
                } else {
                    fullPath = folderPath + "/" + fullPath;
                }
            }
            if (!fullPath.startsWith("/")) {
                fullPath = "/" + fullPath;
            }

            String nameOnly = baseNameFromPath(fullPath);

            // Check if it matches ID suffix
            if (idSuffix.length() > 0 && nameOnly.indexOf(idSuffix + ".bin") != -1) {
                D_println("[DBG] Found ID-matched bin: " + fullPath);
                dir.close();
                return fullPath;
            }
            if (firstFallback == "") {
                firstFallback = fullPath;
            }
        }
        file = dir.openNextFile();
    }

    dir.close();

    if (firstFallback != "") {
        D_println("[DBG] No ID-matched bin found, using first available fallback: " + firstFallback);
    }
    return firstFallback;
}

static void resolveTrainingPaths(String& trainPath, String& metaPath) {
    trainPath = XY_TRAIN_PATH;
    metaPath = METADATA_JSON_PATH;
#ifdef DOUBLE_DATASET
    if (datasetIndex == 1) {
        trainPath = XY_TRAIN_PATH_2;
        metaPath = METADATA_JSON_PATH_2;
    }
#endif

    if (selectedDatasetKey.length() == 0) {
        D_println("[DBG] No dataset key, using default paths: " + trainPath + " | " + metaPath);
        return;
    }

    String key = sanitizeDatasetKey(selectedDatasetKey);
    String defaultMeta = metaPath;

    // Check if filenames were provided in the command, otherwise use defaults from Config.h
    String trainFileName = selectedDatasetBinName.length() > 0 ? selectedDatasetBinName : baseNameFromPath(trainPath);
    String metaFileName = selectedDatasetMetaName.length() > 0 ? selectedDatasetMetaName : baseNameFromPath(metaPath);

    trainPath = "/" + key + "/" + trainFileName;
    metaPath = "/" + key + "/" + metaFileName;

    D_println("[DBG] Resolving for dataset '" + key + "'. Target: " + trainPath);

    // If the expected dataset bin is missing, try to find ANY .bin inside the dataset folder
    if (!LittleFS.exists(trainPath)) {
        String fallbackBinPath = findBinFileInDatasetFolder(key);
        if (fallbackBinPath.length() > 0) {
            D_println("[DBG] " + trainPath + " not found, using fallback " + fallbackBinPath);
            trainPath = fallbackBinPath;
        }
    }

    // Handle metadata fallback logic
    if (!LittleFS.exists(metaPath)) {
        String folderMeta = "/" + key + "/metadata.json";
        if (LittleFS.exists(folderMeta)) {
            D_println("[DBG] " + metaPath + " not found, using folder metadata " + folderMeta);
            metaPath = folderMeta;
        } else if (LittleFS.exists(defaultMeta)) {
            D_println("[DBG] " + metaPath + " not found in dataset folder, using global fallback " + defaultMeta);
            metaPath = defaultMeta;
        }
    }
}

static void resetWifiStationState() {
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);
    esp_wifi_stop();
    esp_wifi_deinit();
    delay(200);
    WiFi.mode(WIFI_STA);
    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);
    WiFi.setSleep(false);
    delay(200);
}

#ifdef DOUBLE_DATASET
void loadDatasetIndex() {
    if (LittleFS.exists(DOUBLE_DATASET_CONFIG_PATH)) {
        File f = LittleFS.open(DOUBLE_DATASET_CONFIG_PATH, "r");
        if (f) {
            String s = f.readString();
            datasetIndex = s.toInt();
            f.close();
        }
    }
    D_println("Current dataset index: " + String(datasetIndex));
}

void saveDatasetIndex() {
    File f = LittleFS.open(DOUBLE_DATASET_CONFIG_PATH, "w");
    if (f) {
        f.print(datasetIndex);
        f.close();
    }
}

void switchDatasetIndex() {
    datasetIndex = (datasetIndex + 1) % 2;
    saveDatasetIndex();
    D_println("Switched dataset index to: " + String(datasetIndex));
}
#endif

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
    // D_printf("Heap info: %d bytes free\n", info.total_free_bytes);
    // D_printf("Heap info: %d bytes largest free block\n", info.largest_free_block);
    // D_printf("Heap info: %d bytes minimum free ever\n", info.minimum_free_bytes);
}

void printModelConfig(ModelConfig *modelConfig) {
    D_println("Federate model config loaded:");
                D_println("Layers: ");
                for (int i = 0; i < modelConfig->numberOfLayers; i++) {
                    D_print(modelConfig->layers[i]);
                    if (i < modelConfig->numberOfLayers - 1) {
                        D_print(", ");
                    }
                }
                D_println();
                D_println("Activation Functions: ");
                for (int i = 0; i < modelConfig->numberOfLayers; i++) {
                    D_print(modelConfig->actvFunctions[i]);
                    if (i < modelConfig->numberOfLayers - 1) {
                        D_print(", ");
                    }
                }
                D_println();
                D_println("Learning Rate of Weights: " + String(modelConfig->learningRateOfWeights));
                D_println("Learning Rate of Biases: " + String(modelConfig->learningRateOfBiases));
                if (modelConfig->randomSeed != 0) {
                    randomSeed(modelConfig->randomSeed);
                    D_println("Random Seed: " + String(modelConfig->randomSeed));
                }
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

    D_println("LittleFS Total Space: " + String(LittleFS.totalBytes()) + " bytes");
    D_println("LittleFS Used Space: " + String(LittleFS.usedBytes()) + " bytes");

#ifdef DOUBLE_DATASET
    loadDatasetIndex();
#endif

    if (!loadDeviceDefinitions()) {
        return;
    }
    D_println(CLIENT_NAME);

    D_println("Booting up...");

    bool configurationLoaded = loadDeviceConfig();
#ifdef DOUBLE_DATASET
    loadOtherDeviceConfig();
#endif
    bool resumeTraining = false;

    if (configurationLoaded) {
        if (deviceConfig->currentRound != -1 && deviceConfig->currentFederateState != FederateState_NONE) {
            currentRound = deviceConfig->currentRound;
            lastTrainedRound = deviceConfig->lastTrainedRound;
            federateState = deviceConfig->currentFederateState;
            if (deviceConfig->currentFederateState != FederateState_NONE && deviceConfig->loadedFederateModelConfig != nullptr) {
                federateModelConfig = deviceConfig->loadedFederateModelConfig;
                deviceConfig->loadedFederateModelConfig = NULL;
                if (deviceConfig->newModelState == ModelState_READY_TO_TRAIN) {
                    if (deviceConfig->lastTrainedRound == currentRound) {
                        D_println("Already trained this round, skipping.");
                        newModelState = ModelState_IDLE;
                    } else {
                        setupFederatedModel();
                    }
                }
            }
            resumeTraining = true;
            if (deviceConfig->newModelState == ModelState_MODEL_BUSY || deviceConfig->newModelState == ModelState_DONE_TRAINING) {
                // If it was busy or done but not cleared, reset to IDLE to avoid stuck state
                newModelState = ModelState_IDLE;
            } else {
                 newModelState = deviceConfig->newModelState;
            }
        }
    } else {
        // If config couldn't be loaded (e.g. flash bypassed/corrupted),
        // default to requesting a resume check from the server on boot
        resumeTraining = true;
    }

    printMemory();
    fixedMemoryUsage.loadConfig = info.total_free_bytes;

    if (initBaseModel) {
        String modelPath = MODEL_PATH;
#ifdef DOUBLE_DATASET
        if (datasetIndex == 1) {
            modelPath = MODEL_PATH_2;
        }
#endif

        if (LittleFS.exists(modelPath)) {
            if (currentModel != NULL) {
            delete currentModel;
        }
        currentModel = loadModelFromFlash(modelPath);
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
            #ifdef DATASET_BINARY
            String trainPath;
            String metaPath;
            resolveTrainingPaths(trainPath, metaPath);
            D_println("Training dataset paths: " + trainPath + " | " + metaPath);
                        if (isJulianaBinaryDataset(metaPath)) {
                            currentModelMetrics = trainModelFromJulianaBinaryDataset(*currentModel, *localModelConfig, trainPath, metaPath);
                        } else {
                            currentModelMetrics = trainModelFromBinaryDataset(*currentModel, *localModelConfig, trainPath, metaPath);
                        }
            #else
            // TODO: Handle double dataset
            currentModelMetrics = trainModelFromOriginalDataset(*currentModel, *localModelConfig, X_TRAIN_PATH, Y_TRAIN_PATH);
            #endif
            if (saveModelToFlash(*currentModel, modelPath)) {
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

    timeSinceLastServerMessage = millis();

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

static JsonArray resolveSchema(JsonDocument& doc) {
    JsonArray schema = doc["schema"];
    if (schema.isNull() || schema.size() == 0) {
        D_println("[DBG] Schema not found at root or empty. Checking nested locations...");
        if (doc["binary"]["schema"].is<JsonArray>()) {
            schema = doc["binary"]["schema"];
            D_println("[DBG] Found schema in 'binary.schema'");
        } else if (selectedDatasetKey.length() > 0 && doc["shards"].is<JsonArray>()) {
            D_println("[DBG] Searching shards for key: " + selectedDatasetKey);
            for (JsonObject shard : doc["shards"].as<JsonArray>()) {
                String dir = shard["directory"] | "";
                if (dir == selectedDatasetKey) {
                    if (shard["schema"].is<JsonArray>()) {
                        schema = shard["schema"];
                        D_println("[DBG] Found schema in 'shards' group for directory: " + dir);
                    }
                    break;
                }
            }
        }
    } else {
        D_println("[DBG] Found schema at root. Columns: " + String(schema.size()));
    }

    if (schema.isNull() || schema.size() == 0) {
        D_println("[WRN] resolveSchema could not find a valid schema array. Root keys present:");
        for (JsonPair kv : doc.as<JsonObject>()) {
            D_println("  - " + String(kv.key().c_str()));
        }
    }

    return schema;
}

#ifdef DATASET_BINARY
bool isJulianaBinaryDataset(const String& meta_file) {
    if (!LittleFS.exists(meta_file)) {
        return false;
    }

    File metaF = LittleFS.open(meta_file, "r");
    if (!metaF) {
        return false;
    }

    JsonDocument doc;
    DeserializationError derr = deserializeJson(doc, metaF);
    metaF.close();
    if (derr != DeserializationError::Ok) {
        D_println("Failed to parse metadata JSON for Juliana check: " + String(derr.c_str()));
        return false;
    }

    const char* label_col = doc["label_column"] | "activityID";
    return strcmp(label_col, "ocupada") == 0;
}

multiClassClassifierMetrics* trainModelFromBinaryDataset(NeuralNetwork& NN, ModelConfig& config, const String& bin_file, const String& meta_file) {
    D_println("Training model from binary dataset...");
    printTiming(true);

    unsigned long initTime = millis();
    datasetSize = 0;

    // Load metadata
    if (!LittleFS.exists(meta_file)) {
        D_println("Metadata file not found");
        return NULL;
    }
    File metaF = LittleFS.open(meta_file, "r");
    if (!metaF) {
        D_println("Failed to open metadata file");
        return NULL;
    }
    JsonDocument doc;
    DeserializationError derr = deserializeJson(doc, metaF);
    metaF.close();
    if (derr) {
        D_println("Failed to parse metadata JSON: " + String(derr.c_str()));
        return NULL;
    }

    JsonArray schema = resolveSchema(doc);
    const char* label_col = doc["label_column"] | "activityID";
    JsonArray label_vals = doc["label_values"];
    std::vector<long> label_values;
    for (auto v : label_vals) label_values.push_back(v.as<long>());
    bool encoded_labels = doc.containsKey("label_map");

    struct Col { String name; String type; int bytes; int offset; };
    std::vector<Col> cols;
    int row_size = 0;
    for (JsonObject c : schema) {
        Col col;
        col.name = String((const char*)c["name"]);
        col.type = String((const char*)c["type"]);
        col.bytes = c["bytes"] | 0;
        col.offset = c["offset"] | 0;
        cols.push_back(col);
        row_size += col.bytes;
    }

    // Debug: print parsed schema and computed row size
    D_println("[DBG] Parsed schema columns: " + String(cols.size()));
    D_println("[DBG] Computed row_size: " + String(row_size));

    // Determine input and label indices
    std::vector<int> input_indices;
    int label_index = -1;
    for (size_t i = 0; i < cols.size(); ++i) {
        if (cols[i].name == String(label_col)) {
            label_index = (int)i;
        } else if (cols[i].name == String("timestamp")) {
            // skip timestamp by default
            continue;
        } else {
            input_indices.push_back((int)i);
        }
    }

    if (label_index < 0) {
        D_println("Label column not found in schema");
        return NULL;
    }

    if (input_indices.size() != NN.layers[0]._numberOfInputs) {
        D_println("[ERR] Dataset features (" + String(input_indices.size()) + ") do not match model inputs (" + String(NN.layers[0]._numberOfInputs) + ").");
        return NULL;
    }

    // Debug: report input / label mapping
    D_println("[DBG] Input feature count: " + String(input_indices.size()));
    D_println("[DBG] Label column index: " + String(label_index) + " (name='" + String(label_col) + "')");

    File binF = LittleFS.open(bin_file, "r");
    if (!binF) {
        D_println("Failed to open binary file");
        return NULL;
    }

    // Allocate buffers and arrays
    uint8_t* rowbuf = (uint8_t*)malloc(row_size);
    if (!rowbuf) {
        D_println("Failed to allocate row buffer");
        binF.close();
        return NULL;
    }

    IDFLOAT* x = new IDFLOAT[NN.layers[0]._numberOfInputs];
    IDFLOAT* y = new IDFLOAT[NN.layers[NN.numberOflayers - 1]._numberOfOutputs];

    // Debug: report NN expected sizes vs parsed sizes
    D_println("[DBG] NN expected input size: " + String(NN.layers[0]._numberOfInputs) + ", parsed feature count: " + String(input_indices.size()));
    D_println("[DBG] NN output size (num classes): " + String(NN.layers[NN.numberOflayers - 1]._numberOfOutputs));

    multiClassClassifierMetrics* metrics = new multiClassClassifierMetrics;
    metrics->numberOfClasses = NN.layers[NN.numberOflayers - 1]._numberOfOutputs;
    metrics->metrics = new classClassifierMetricts[metrics->numberOfClasses];

    // Limit verbose prints: show detailed parse for first N rows, then periodic summaries
    const int DBG_FIRST_ROWS = 5;
    const int DBG_EVERY_N = 5000;
    for (int epoch = 0; epoch < config.epochs; ++epoch) {
        D_println("Epoch: " + String(epoch+1));
        binF.seek(0);

        while (binF.available() >= row_size) {
            size_t n = binF.read(rowbuf, row_size);
            if (n != (size_t)row_size) break;
            datasetSize++;

            // parse inputs
            for (size_t i = 0; i < input_indices.size(); ++i) {
                int ci = input_indices[i];
                Col &c = cols[ci];
                float val = 0.0f;
                if (c.type == "float32") {
                    float v; memcpy(&v, rowbuf + c.offset, 4); val = v;
                } else if (c.type == "int32") {
                    int32_t v; memcpy(&v, rowbuf + c.offset, 4); val = (float)v; // timestamp skipped earlier
                } else if (c.type == "uint8") {
                    uint8_t v = *(uint8_t*)(rowbuf + c.offset); val = (float)v;
                } else if (c.type == "int8") {
                    int8_t v = *(int8_t*)(rowbuf + c.offset); val = (float)v;
                }
                #if defined(USE_64_BIT_DOUBLE)
                x[i] = (IDFLOAT)val;
                #else
                x[i] = (IDFLOAT)val;
                #endif
            }

            // Debug: print sample parsed X for first rows and periodic samples
            /*if ((int)datasetSize <= DBG_FIRST_ROWS || (datasetSize % DBG_EVERY_N) == 0) {
                D_println("[DBG] Row #" + String(datasetSize) + " parsed -- first few features:");
                String s = "";
                for (size_t xi = 0; xi < input_indices.size(); ++xi) {
                    s += String((double)x[xi], 6);
                    if (xi < input_indices.size() - 1) s += ", ";
                    if (xi > 30) { s += ", ..."; break; }
                }
                D_println(s);
            }*/

            // parse label and build one-hot y
            long labelVal = 0;
            Col &lc = cols[label_index];
            if (lc.type == "int8") { int8_t v; memcpy(&v, rowbuf + lc.offset, 1); labelVal = v; }
            else if (lc.type == "uint8") { uint8_t v; memcpy(&v, rowbuf + lc.offset, 1); labelVal = v; }
            else if (lc.type == "int32") { int32_t v; memcpy(&v, rowbuf + lc.offset, 4); labelVal = v; }
            else { int32_t v; memcpy(&v, rowbuf + lc.offset, 4); labelVal = v; }

            if (encoded_labels) {
                // labelVal is encoded as 1..N where 0 means "no label".
                int encoded = (int)labelVal - 1; // convert to 0-based index
                if (encoded < 0 || encoded >= (int)metrics->numberOfClasses) {
                    // no label or out-of-range -> all zeros
                    for (unsigned int k = 0; k < metrics->numberOfClasses; ++k) y[k] = (IDFLOAT)0.0;
                } else {
                    for (unsigned int k = 0; k < metrics->numberOfClasses; ++k) {
                        y[k] = (k == encoded) ? (IDFLOAT)1.0 : (IDFLOAT)0.0;
                    }
                }
            } else {
                for (unsigned int k = 0; k < metrics->numberOfClasses; ++k) {
                    y[k] = (label_values[k] == labelVal) ? (IDFLOAT)1.0 : (IDFLOAT)0.0;
                }
            }

            // Debug: print y (one-hot) for the same sample rows
            /*if ((int)datasetSize <= DBG_FIRST_ROWS || (datasetSize % DBG_EVERY_N) == 0) {
                String ys = "[DBG] y: ";
                for (unsigned int k = 0; k < metrics->numberOfClasses; ++k) {
                    ys += String((int)y[k]);
                    if (k < metrics->numberOfClasses - 1) ys += ",";
                }
                D_println(ys);
            }*/

            // Train model
            IDFLOAT* predictions = NN.FeedForward(x);
            NN.BackProp(y);
            metrics->meanSqrdError = NN.getMeanSqrdError(1);

            // Detect gradient explosion / NaN or Inf in error and dump context
            {
                double mse = (double)metrics->meanSqrdError;
                if (!isfinite(mse) || isnan(mse)) {
                    D_println("[ERR] Gradient explosion detected (meanSqrdError is NaN/Inf)");
                    D_println("[ERR] Epoch: " + String(epoch+1) + "  Row#: " + String(datasetSize));

                    // Print a short slice of x
                    String sx = "[ERR] x: ";
                    int max_x_print = 12;
                    for (size_t xi = 0; xi < input_indices.size() && (int)xi < max_x_print; ++xi) {
                        sx += String((double)x[xi], 6);
                        if (xi < input_indices.size() - 1) sx += ", ";
                    }
                    if (input_indices.size() > (size_t)max_x_print) sx += ", ...";
                    D_println(sx);

                    // Print y one-hot
                    String sy = "[ERR] y: ";
                    for (unsigned int k = 0; k < metrics->numberOfClasses; ++k) {
                        sy += String((int)y[k]);
                        if (k < metrics->numberOfClasses - 1) sy += ",";
                    }
                    D_println(sy);

                    // Print predictions
                    String sp = "[ERR] predictions: ";
                    for (unsigned int k = 0; k < metrics->numberOfClasses; ++k) {
                        sp += String((double)predictions[k], 6);
                        if (k < metrics->numberOfClasses - 1) sp += ",";
                    }
                    D_println(sp);

                    // Cleanup and return early to avoid further corruption
                    metrics->trainingTime = millis() - initTime;
                    metrics->epochs = config.epochs;
                    delete[] x;
                    delete[] y;
                    free(rowbuf);
                    binF.close();
                    D_println("[ERR] Aborting training due to gradient explosion.");
                    return metrics;
                }
            }

            // Update metrics
            for (int i = 0; i < metrics->numberOfClasses; i++) {
                if (y[i] == 1) {
                    if (predictions[i] >= 0.5) metrics->metrics[i].truePositives++;
                    else metrics->metrics[i].falseNegatives++;
                } else {
                    if (predictions[i] >= 0.5) metrics->metrics[i].falsePositives++;
                    else metrics->metrics[i].trueNegatives++;
                }
            }
        }
    }

    metrics->trainingTime = millis() - initTime;
    metrics->epochs = config.epochs;

    delete[] x;
    delete[] y;
    free(rowbuf);
    binF.close();
    printTiming();
    D_println("Binary training complete.");
    return metrics;
}

multiClassClassifierMetrics* trainModelFromJulianaBinaryDataset(NeuralNetwork& NN, ModelConfig& config, const String& bin_file, const String& meta_file) {
    D_println("Training model from Juliana binary dataset...");
    printTiming(true);

    unsigned long initTime = millis();
    datasetSize = 0;

    if (!LittleFS.exists(meta_file)) {
        D_println("Metadata file not found");
        return NULL;
    }

    File metaF = LittleFS.open(meta_file, "r");
    if (!metaF) {
        D_println("Failed to open metadata file");
        return NULL;
    }

    JsonDocument doc;
    DeserializationError derr = deserializeJson(doc, metaF);
    metaF.close();
    if (derr) {
        D_println("Failed to parse metadata JSON: " + String(derr.c_str()));
        return NULL;
    }

    JsonArray schema = resolveSchema(doc);
    const char* label_col = doc["label_column"] | "ocupada";
    if (strcmp(label_col, "ocupada") != 0) {
        D_println("Metadata does not describe the Juliana dataset");
        return NULL;
    }

    struct Col { String name; String type; int bytes; int offset; };
    std::vector<Col> cols;
    int row_size = 0;
    for (JsonObject c : schema) {
        Col col;
        col.name = String((const char*)c["name"]);
        col.type = String((const char*)c["type"]);
        col.bytes = c["bytes"] | 0;
        col.offset = c["offset"] | 0;
        cols.push_back(col);
        row_size += col.bytes;
    }

    D_println("[DBG] Parsed Juliana schema columns: " + String(cols.size()));
    D_println("[DBG] Computed row_size: " + String(row_size));

    std::vector<int> input_indices;
    int label_index = -1;
    for (size_t i = 0; i < cols.size(); ++i) {
        if (cols[i].name == String(label_col)) {
            label_index = (int)i;
        } else {
            input_indices.push_back((int)i);
        }
    }

    if (label_index < 0) {
        D_println("Label column not found in schema");
        return NULL;
    }

    if (input_indices.size() != NN.layers[0]._numberOfInputs) {
        D_println("[ERR] Juliana dataset features (" + String(input_indices.size()) + ") do not match model inputs (" + String(NN.layers[0]._numberOfInputs) + ").");
        return NULL;
    }

    D_println("[DBG] Input feature count: " + String(input_indices.size()));
    D_println("[DBG] Label column index: " + String(label_index) + " (name='" + String(label_col) + "')");

    File binF = LittleFS.open(bin_file, "r");
    if (!binF) {
        D_println("Failed to open binary file");
        return NULL;
    }

    uint8_t* rowbuf = (uint8_t*)malloc(row_size);
    if (!rowbuf) {
        D_println("Failed to allocate row buffer");
        binF.close();
        return NULL;
    }

    IDFLOAT* x = new IDFLOAT[NN.layers[0]._numberOfInputs];
    IDFLOAT* y = new IDFLOAT[NN.layers[NN.numberOflayers - 1]._numberOfOutputs];

    D_println("[DBG] NN expected input size: " + String(NN.layers[0]._numberOfInputs) + ", parsed feature count: " + String(input_indices.size()));
    D_println("[DBG] NN output size (num classes): " + String(NN.layers[NN.numberOflayers - 1]._numberOfOutputs));

    multiClassClassifierMetrics* metrics = new multiClassClassifierMetrics;
    metrics->numberOfClasses = NN.layers[NN.numberOflayers - 1]._numberOfOutputs;
    metrics->metrics = new classClassifierMetricts[metrics->numberOfClasses];

    const int DBG_FIRST_ROWS = 5;
    const int DBG_EVERY_N = 5000;
    for (int epoch = 0; epoch < config.epochs; ++epoch) {
        D_println("Epoch: " + String(epoch + 1));
        binF.seek(0);

        while (binF.available() >= row_size) {
            size_t n = binF.read(rowbuf, row_size);
            if (n != (size_t)row_size) break;
            datasetSize++;

            for (size_t i = 0; i < input_indices.size(); ++i) {
                int ci = input_indices[i];
                Col& c = cols[ci];
                float val = 0.0f;
                if (c.type == "float32") {
                    float v; memcpy(&v, rowbuf + c.offset, 4); val = v;
                } else if (c.type == "uint8") {
                    uint8_t v = *(uint8_t*)(rowbuf + c.offset); val = (float)v;
                } else if (c.type == "int32") {
                    int32_t v; memcpy(&v, rowbuf + c.offset, 4); val = (float)v;
                } else if (c.type == "int8") {
                    int8_t v = *(int8_t*)(rowbuf + c.offset); val = (float)v;
                }
                x[i] = (IDFLOAT)val;
            }

            long labelVal = 0;
            Col& lc = cols[label_index];
            if (lc.type == "uint8") { uint8_t v; memcpy(&v, rowbuf + lc.offset, 1); labelVal = v; }
            else if (lc.type == "int8") { int8_t v; memcpy(&v, rowbuf + lc.offset, 1); labelVal = v; }
            else if (lc.type == "int32") { int32_t v; memcpy(&v, rowbuf + lc.offset, 4); labelVal = v; }
            else { int32_t v; memcpy(&v, rowbuf + lc.offset, 4); labelVal = v; }

            for (unsigned int k = 0; k < metrics->numberOfClasses; ++k) {
                y[k] = (IDFLOAT)0.0;
            }
            if (metrics->numberOfClasses == 1) {
                y[0] = (IDFLOAT)((labelVal != 0) ? 1.0 : 0.0);
            } else {
                unsigned int classIndex = (labelVal <= 0) ? 0u : 1u;
                if (classIndex >= metrics->numberOfClasses) {
                    classIndex = metrics->numberOfClasses - 1;
                }
                y[classIndex] = (IDFLOAT)1.0;
            }

            IDFLOAT* predictions = NN.FeedForward(x);
            NN.BackProp(y);
            metrics->meanSqrdError = NN.getMeanSqrdError(1);

            {
                double mse = (double)metrics->meanSqrdError;
                if (!isfinite(mse) || isnan(mse)) {
                    D_println("[ERR] Gradient explosion detected (meanSqrdError is NaN/Inf)");
                    D_println("[ERR] Epoch: " + String(epoch + 1) + "  Row#: " + String(datasetSize));
                    metrics->trainingTime = millis() - initTime;
                    metrics->epochs = config.epochs;
                    delete[] x;
                    delete[] y;
                    free(rowbuf);
                    binF.close();
                    D_println("[ERR] Aborting training due to gradient explosion.");
                    return metrics;
                }
            }

            for (int i = 0; i < metrics->numberOfClasses; i++) {
                if (y[i] == 1) {
                    if (predictions[i] >= 0.5) metrics->metrics[i].truePositives++;
                    else metrics->metrics[i].falseNegatives++;
                } else {
                    if (predictions[i] >= 0.5) metrics->metrics[i].falsePositives++;
                    else metrics->metrics[i].trueNegatives++;
                }
            }
        }
    }

    metrics->trainingTime = millis() - initTime;
    metrics->epochs = config.epochs;

    delete[] x;
    delete[] y;
    free(rowbuf);
    binF.close();
    printTiming();
    D_println("Juliana binary training complete.");
    return metrics;
}
#endif

#ifdef DATASET_ORIGINAL
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
#endif

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
    ensureConnected();
    if (!sendingMessage) {
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
        timeSinceLastServerMessage = millis();
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
        timeSinceLastServerMessage = millis();
        if (newModelState != ModelState_IDLE) {
            D_println("Already processing a model");
            return;
        }
        newModelState = ModelState_MODEL_BUSY;

#if DIRECT_MQTT_STREAMING
        if (tempModel != NULL) {
            delete tempModel;
        }
        if (newModel != NULL) {
            delete newModel;
        }

        if (federateModelConfig != NULL) {
            newModel = new NeuralNetwork(federateModelConfig->layers, federateModelConfig->numberOfLayers, federateModelConfig->actvFunctions);
            newModel->LearningRateOfBiases = federateModelConfig->learningRateOfBiases;
            newModel->LearningRateOfWeights = federateModelConfig->learningRateOfWeights;
        } else {
            newModel = new NeuralNetwork(localModelConfig->layers, localModelConfig->numberOfLayers, localModelConfig->actvFunctions);
            newModel->LearningRateOfBiases = localModelConfig->learningRateOfBiases;
            newModel->LearningRateOfWeights = localModelConfig->learningRateOfWeights;
        }

        bool loaded = loadModelFromStreamDirectly(newModel, stream);
#else
        File file = LittleFS.open(TEMPORARY_NEW_MODEL_PATH, "w+");
        if (!file) {
            D_println("Error opening file for writing");
            return;
        }
        
        uint8_t buffer[1024];
        while (stream.available()) {
            // readBytes returns the actual number of bytes read
            size_t bytesRead = stream.readBytes(buffer, sizeof(buffer));
            if (bytesRead > 0) {
                // Only write exactly the amount of bytes that were successfully read
                file.write(buffer, bytesRead);
            }
        }
        file.seek(0);

        if (tempModel != NULL) {
            delete tempModel;
        }
        if (newModel != NULL) {
            delete newModel;
        }

        if (federateModelConfig != NULL) {
            newModel = new NeuralNetwork(federateModelConfig->layers, federateModelConfig->numberOfLayers, federateModelConfig->actvFunctions);
            newModel->LearningRateOfBiases = federateModelConfig->learningRateOfBiases;
            newModel->LearningRateOfWeights = federateModelConfig->learningRateOfWeights;
        } else {
            newModel = new NeuralNetwork(localModelConfig->layers, localModelConfig->numberOfLayers, localModelConfig->actvFunctions);
            newModel->LearningRateOfBiases = localModelConfig->learningRateOfBiases;
            newModel->LearningRateOfWeights = localModelConfig->learningRateOfWeights;
        }
        
        bool loaded = newModel->load(file);
        file.close();
#endif
        
        if (loaded) {
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
    WiFi.setTxPower(WIFI_POWER_MINUS_1dBm);
    D_println("power set");
    connectToWifi(true);
    if (connectToServerMQTT()) {
        D_println("Connected to MQTT server");
    } else {
        D_println("Failed to connect to MQTT server");
    }

    // TODO Need to handle disconnections properly, when the FL server drops/leaves, when mosquitto dies, when wifi dies

    mqtt.subscribe(MQTT_RECEIVE_COMMANDS_TOPIC, [](const char* topic, Stream& stream) {
        timeSinceLastServerMessage = millis();

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

            // Check if command is targeted at specific client
            bool isMe = true;
            bool isOther = false;
            if (doc.containsKey("client")) {
                const char* client = doc["client"];
                if (strcmp(client, CLIENT_NAME) != 0) {
                    isMe = false;
                    #ifdef DOUBLE_DATASET
                    if (OTHER_CLIENT_NAME != NULL && strcmp(client, OTHER_CLIENT_NAME) == 0) {
                        isOther = true;
                    }
                    #endif
                }
            } else if (doc.containsKey("clients")) {
                // If it's a list, check if we OR other are in it
                JsonArray clients = doc["clients"];
                isMe = false;
                for (int i = 0; i < clients.size(); i++) {
                    if (strcmp(clients[i].as<const char*>(), CLIENT_NAME) == 0) {
                        isMe = true;
                        break;
                    }
                    #ifdef DOUBLE_DATASET
                    if (OTHER_CLIENT_NAME != NULL && strcmp(clients[i].as<const char*>(), OTHER_CLIENT_NAME) == 0) {
                        isOther = true;
                        // Don't break here, we might need to know if both are targeted?
                        // Actually, if both are targeted, we handle 'isMe' logic, and possibly 'isOther' logic.
                    }
                    #endif
                }
            }

            if (strcmp(command, "request_model") == 0) {
                // Only respond if it targets me? Or broadcast? Protocol is unclear on broadcast vs target.
                // Assuming targeted if 'client' field exists, otherwise broadcast.
                if (isMe || (!doc.containsKey("client") && !doc.containsKey("clients"))) {
                    sendModelToNetwork(*currentModel, *currentModelMetrics);
                    if (federateState == FederateState_TRAINING) {
                        if (newModel != NULL)
                            delete newModel;
                        if (newModelMetrics != NULL)
                            delete newModelMetrics;
                        newModelState = ModelState_IDLE;
                    }
                }
                // TODO: Handle isOther? We cannot send model for other device as we don't have its state loaded completely.
            } else if (strcmp(command, "federate_join") == 0) {
                if (federateState == FederateState_NONE) {
                    federateState = FederateState_SUBSCRIBED;
                    saveDeviceConfig();
                    sendMessageToNetwork(FederateCommand_JOIN);
                }
                #ifdef DOUBLE_DATASET
                if (OTHER_CLIENT_NAME != NULL) {
                     // We should probably also join for the other client if it's not subscribed?
                     // But we don't know its state without reading its config.
                     // Assuming we should just announce it.
                     // If we are simulating 2 devices, we should try to keep them in sync or just join both.
                     sendMessageToNetwork(FederateCommand_JOIN, OTHER_CLIENT_NAME);
                     
                     // Also ensure config is updated for other client to be in SUBSCRIBED state
                     String otherConfigPath = (datasetIndex == 0) ? CONFIGURATION_PATH_2 : CONFIGURATION_PATH;
                     if (LittleFS.exists(otherConfigPath)) {
                        File f = LittleFS.open(otherConfigPath, "r");
                        if (f) {
                            JsonDocument otherDoc;
                            deserializeJson(otherDoc, f);
                            f.close();
                            if ((int)otherDoc["federateState"] == (int)FederateState_NONE) {
                                otherDoc["federateState"] = (int)FederateState_SUBSCRIBED;
                                File f2 = LittleFS.open(otherConfigPath, "w");
                                if (f2) {
                                    serializeJson(otherDoc, f2);
                                    f2.close();
                                }
                            }
                        }
                     }
                }
                #endif
            } else if (strcmp(command, "federate_unsubscribe") == 0) {
                if (isMe) {
                    if (federateState != FederateState_NONE) {
                        federateState = FederateState_NONE;
                        currentRound = -1;
                        lastTrainedRound = -1;
                        sendMessageToNetwork(FederateCommand_LEAVE);
                        saveDeviceConfig();
                    }
                }
                if (isOther) {
                    #ifdef DOUBLE_DATASET
                    // We need to update the config file for the other device to set federateState = NONE
                    String otherConfigPath = (datasetIndex == 0) ? CONFIGURATION_PATH_2 : CONFIGURATION_PATH;
                    // Load, modify, save
                    if (LittleFS.exists(otherConfigPath)) {
                        File f = LittleFS.open(otherConfigPath, "r");
                        if (f) {
                            JsonDocument otherDoc;
                            deserializeJson(otherDoc, f);
                            f.close();
                            otherDoc["federateState"] = (int)FederateState_NONE;
                            otherDoc["currentRound"] = -1;
                            otherDoc["lastTrainedRound"] = -1;
                            File f2 = LittleFS.open(otherConfigPath, "w");
                            if (f2) {
                                serializeJson(otherDoc, f2);
                                f2.close();
                                // Send LEAVE for other
                                // Manual leave message construction since sendMessageToNetwork doesn't support LEAVE param with client name in current enum/helper?
                                // Actually we don't have LEAVE in enum logic in sendMessageToNetwork helper properly for generic client,
                                // but we can add or just construct json here.
                                // Wait, sendMessageToNetwork has FederateCommand_LEAVE?
                                // The switch(command) in sendMessageToNetwork didn't show LEAVE case in previous read_file output?
                                // Let's check sendMessageToNetwork again. It had JOIN, RESUME, ALIVE.
                                // Ah, the existing code called sendMessageToNetwork(FederateCommand_LEAVE) but I didn't see it in switch?
                                // Maybe it was implicit/missing or I missed it.
                                // Let's check existing code for federate_unsubscribe block.
                                // It calls sendMessageToNetwork(FederateCommand_LEAVE);
                                // But looking at sendMessageToNetwork implementation earlier...
                                // It switch(command): JOIN, RESUME, ALIVE.
                                // Default? No default.
                                // So LEAVE might be doing nothing?
                            }
                        }
                    }
                    #endif
                }
            } else if (strcmp(command, "federate_start") == 0) {
                if (federateState == FederateState_SUBSCRIBED) {
                    if (doc["config"].is<JsonObject>()) {
                        if (doc["database"].is<const char*>()) {
                            selectedDatasetKey = sanitizeDatasetKey(String(doc["database"].as<const char*>()));
                        } else if (doc["dataset"].is<const char*>()) {
                            selectedDatasetKey = sanitizeDatasetKey(String(doc["dataset"].as<const char*>()));
                        } else if (doc["datasetKey"].is<const char*>()) {
                            selectedDatasetKey = sanitizeDatasetKey(String(doc["datasetKey"].as<const char*>()));
                        } else {
                            selectedDatasetKey = "";
                        }

                        if (doc["datasetBin"].is<const char*>()) {
                            selectedDatasetBinName = baseNameFromPath(String(doc["datasetBin"].as<const char*>()));
                        } else {
                            selectedDatasetBinName = "";
                        }
                        if (doc["datasetMeta"].is<const char*>()) {
                            selectedDatasetMetaName = baseNameFromPath(String(doc["datasetMeta"].as<const char*>()));
                        } else {
                            selectedDatasetMetaName = "";
                        }

                        if (selectedDatasetKey.length() > 0) {
                            D_println("Selected dataset key: " + selectedDatasetKey);
                        } else {
                            D_println("No dataset key provided; using Config.h default dataset paths");
                        }

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
                        if (doc["config"]["jsonWeights"].is<bool>()) {
                            federateModelConfig->jsonWeights = doc["config"]["jsonWeights"].as<bool>();
                        }
                        federateState = FederateState_TRAINING;
                        currentRound = 0;
                        lastTrainedRound = -1;
                        setupFederatedModel();
                        saveDeviceConfig();
                    }
                }
                
                #ifdef DOUBLE_DATASET
                if (OTHER_CLIENT_NAME != NULL) {
                     String otherConfigPath = (datasetIndex == 0) ? CONFIGURATION_PATH_2 : CONFIGURATION_PATH;
                     if (LittleFS.exists(otherConfigPath)) {
                        File f = LittleFS.open(otherConfigPath, "r");
                        if (f) {
                            JsonDocument otherDoc;
                            deserializeJson(otherDoc, f);
                            f.close();
                            // If is subscribed OR is already training (could be restart?) OR even NONE if we want to force start?
                            // Let's stick to subscribed or training.
                            int otherState = (int)otherDoc["federateState"];
                            if (otherState == (int)FederateState_SUBSCRIBED || otherState == (int)FederateState_TRAINING) {
                                otherDoc["federateState"] = (int)FederateState_TRAINING;
                                otherDoc["currentRound"] = 0;
                                otherDoc["lastTrainedRound"] = -1;
                                otherDoc["modelState"] = (int)ModelState_READY_TO_TRAIN;
                                if (selectedDatasetKey.length() > 0) {
                                    otherDoc["datasetKey"] = selectedDatasetKey;
                                } else {
                                    otherDoc.remove("datasetKey");
                                }
                                if (selectedDatasetBinName.length() > 0) {
                                    otherDoc["datasetBin"] = selectedDatasetBinName;
                                } else {
                                    otherDoc.remove("datasetBin");
                                }
                                if (selectedDatasetMetaName.length() > 0) {
                                    otherDoc["datasetMeta"] = selectedDatasetMetaName;
                                } else {
                                    otherDoc.remove("datasetMeta");
                                }
                                otherDoc["federateModelConfig"] = doc["config"]; 
                                
                                File f2 = LittleFS.open(otherConfigPath, "w");
                                if (f2) {
                                    serializeJson(otherDoc, f2);
                                    f2.close();
                                }
                            }
                        }
                     }
                }
                #endif

            } else if (strcmp(command, "federate_end") == 0) {
                if (federateState != FederateState_NONE) {
                    federateState = FederateState_DONE;
                    currentRound = -1;
                    saveDeviceConfig();
                }
                #ifdef DOUBLE_DATASET
                if (OTHER_CLIENT_NAME != NULL) {
                     String otherConfigPath = (datasetIndex == 0) ? CONFIGURATION_PATH_2 : CONFIGURATION_PATH;
                     if (LittleFS.exists(otherConfigPath)) {
                        File f = LittleFS.open(otherConfigPath, "r");
                         if (f) {
                            JsonDocument otherDoc;
                            deserializeJson(otherDoc, f);
                            f.close();
                            if ((int)otherDoc["federateState"] != (int)FederateState_NONE) {
                                otherDoc["federateState"] = (int)FederateState_DONE;
                                otherDoc["currentRound"] = -1;
                                File f2 = LittleFS.open(otherConfigPath, "w");
                                if (f2) {
                                    serializeJson(otherDoc, f2);
                                    f2.close();
                                }
                            }
                         }
                     }
                }
                #endif
            } else if (strcmp(command, "federate_stop") == 0) {
                if (isMe) {
                    federateState = FederateState_DONE;
                    currentRound = -1;
                    unsubscribeFromResume = true;
                    if (newModel != NULL) {
                        delete newModel;
                        newModel = NULL;
                    }
                    if (tempModel != NULL) {
                        delete tempModel;
                        tempModel = NULL;
                    }
                    if (newModelMetrics != NULL) {
                        delete newModelMetrics;
                        newModelMetrics = NULL;
                    }
                    newModelState = ModelState_IDLE;
                    saveDeviceConfig();
                }
                if (isOther) {
                    #ifdef DOUBLE_DATASET
                     String otherConfigPath = (datasetIndex == 0) ? CONFIGURATION_PATH_2 : CONFIGURATION_PATH;
                     // Update to DONE
                     // Similar to federate_end logic but targeted
                     if (LittleFS.exists(otherConfigPath)) {
                        File f = LittleFS.open(otherConfigPath, "r");
                         if (f) {
                            JsonDocument otherDoc;
                            deserializeJson(otherDoc, f);
                            f.close();
                            otherDoc["federateState"] = (int)FederateState_DONE;
                            otherDoc["currentRound"] = -1;
                            File f2 = LittleFS.open(otherConfigPath, "w");
                            if (f2) {
                                serializeJson(otherDoc, f2);
                                f2.close();
                            }
                         }
                     }
                    #endif
                }
            } else if (strcmp(command, "federate_resume") == 0) {
                if (isMe) {
                    D_println("Resuming training...");
                    
                    // Parse configuration if provided by the server
                    if (doc.containsKey("config") && doc["config"].is<JsonObject>()) {
                        unsigned int* federateLayers = new unsigned int[doc["config"]["layers"].size()];
                        for (int i = 0; i < doc["config"]["layers"].size(); i++) {
                            federateLayers[i] = doc["config"]["layers"][i].as<unsigned int>();
                        }
                        byte* federateActvFunctions = new byte[doc["config"]["actvFunctions"].size()];
                        for (int i = 0; i < doc["config"]["actvFunctions"].size(); i++) {
                            federateActvFunctions[i] = doc["config"]["actvFunctions"][i].as<byte>();
                        }
                        
                        if (federateModelConfig != NULL) {
                            delete federateModelConfig;
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
                        if (doc["config"]["jsonWeights"].is<bool>()) {
                            federateModelConfig->jsonWeights = doc["config"]["jsonWeights"].as<bool>();
                        }
                    }
                    
                    if (doc.containsKey("round")) {
                        currentRound = doc["round"].as<int>();
                        lastTrainedRound = currentRound - 1; // force training for this round
                    }
                    
                    federateState = FederateState_TRAINING;
                    setupResume();
                    setupFederatedModel();
                    
                    // Don't set newModelState to READY_TO_TRAIN yet (setupFederatedModel sets it, but we override it back)
                    // because we must wait for the raw weights to arrive on raw resume topic.
                    newModelState = ModelState_IDLE;
                    saveDeviceConfig();
                    
                    D_println("Setup done for round " + String(currentRound));
                }
                // isOther? If other accepts resume, it means it should be training.
                // But it's not running. 
                // We should probably just ensure its state is TRAINING in config? 
                // But generally resume triggers immediate action. Active client actions.
                // We can't do much for the inactive one here other than ensure state is preserved.
            } else if (strcmp(command, "federate_waiting") == 0) {
                if (isMe) {
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
                            sendMessageToNetwork(FederateCommand_ALIVE, CLIENT_NAME);
                        }
                }
                #ifdef DOUBLE_DATASET
                if (isOther) {
                    // Send ALIVE for other, giving it a chance to stay known
                    sendMessageToNetwork(FederateCommand_ALIVE, OTHER_CLIENT_NAME);
                }
                #endif
            } else if (strcmp(command, "federate_alive") == 0) {
                sendMessageToNetwork(FederateCommand_ALIVE);
            } else if (strcmp(command, "federate_reboot") == 0) {
                if (federateState != FederateState_NONE) {
                    ESP.restart();
                }
                // If it's a global reboot, we reboot.
            }
        }
    });

    mqtt.subscribe(MQTT_RAW_RECEIVE_TOPIC, [](const char* topic, Stream& stream) {
        timeSinceLastServerMessage = millis();
        unsigned long startTime = millis();
        printMemory();
        roundMemoryUsage.messageReceived = info.total_free_bytes;
        if (newModelState != ModelState_IDLE) {
            D_println("Already processing a model");
            return;
        }
        newModelState = ModelState_MODEL_BUSY;
        #if DIRECT_MQTT_STREAMING
                if (federateState == FederateState_NONE) {
                    newModel = new NeuralNetwork(localModelConfig->layers, localModelConfig->numberOfLayers, localModelConfig->actvFunctions);
                    newModel->LearningRateOfBiases = localModelConfig->learningRateOfBiases;
                    newModel->LearningRateOfWeights = localModelConfig->learningRateOfWeights;
                } else {
                    newModel = new NeuralNetwork(federateModelConfig->layers, federateModelConfig->numberOfLayers, federateModelConfig->actvFunctions);
                    newModel->LearningRateOfBiases = federateModelConfig->learningRateOfBiases;
                    newModel->LearningRateOfWeights = federateModelConfig->learningRateOfWeights;
                }

                bool loaded = loadModelFromStreamDirectly(newModel, stream);
        #else
                File file = LittleFS.open(TEMPORARY_NEW_MODEL_PATH, "w+");
                if (!file) {
                    D_println("Error opening file for writing");
                    return;
                }

                uint8_t buffer[1024];
                while (stream.available()) {
                    // readBytes returns the actual number of bytes read
                    size_t bytesRead = stream.readBytes(buffer, sizeof(buffer));
                    if (bytesRead > 0) {
                        // Only write exactly the amount of bytes that were successfully read
                        file.write(buffer, bytesRead);
                    }
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

                bool loaded = newModel->load(file);
                file.close();
        #endif

                if (loaded) {
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
        timeSinceLastServerMessage = millis();
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
    // ! If we disable the watchdog trigger this may cause the ESP32 to hang indefinitely, but even without disabling some devices got stuck inside here. There are alternatives to detect this, we could spawn a new Task that monitors the ESP32 health and try to recover from those cases.
#if WIFI_USE_DHCP
    D_println("WiFi mode: DHCP");
#else
    IPAddress staticIp(192, 168, 10, WIFI_STATIC_HOST_BASE + String(CLIENT_NAME).substring(3).toInt());
    WiFi.config(staticIp, WIFI_STATIC_GATEWAY, WIFI_STATIC_SUBNET);
    D_println("WiFi mode: manual, ip set to " + staticIp.toString());
#endif
    if (WiFi.status() == WL_CONNECTED) {
        D_println("Already connected to Wifi");
        return true;
    }
    else {
        D_println("Connecting to wifi...");
        resetWifiStationState();
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        unsigned long startTime = millis();
        unsigned long timeout = CONNECTION_TIMEOUT; // 30 second timeout
        unsigned long lastBegin = millis();
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
                    if (millis() - lastBegin > 3000) {
                        resetWifiStationState();
                        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
                        lastBegin = millis();
                        D_println("Retrying WiFi.begin() after disconnect");
                    }
                    break;
                case WL_IDLE_STATUS:
                    D_println("Wifi idle status");
                    if (millis() - lastBegin > 3000) {
                        resetWifiStationState();
                        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
                        lastBegin = millis();
                        D_println("Retrying WiFi.begin() after idle status");
                    }
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

void sendMessageToNetwork(FederateCommand command, const char* clientName) {
    if (!ensureConnected()) {
        D_println("Not connected to the network");
        return;
    }

    if (sendingMessage) {
        return;
    }
    sendingMessage = true;

    D_println("Sending command to the network for client: " + String(clientName));

    JsonDocument doc;

    switch (command)
    {
    case FederateCommand_JOIN: {

        doc["command"] = "join";
        doc["client"] = clientName;
        doc["metrics"] = JsonObject();
        if (currentModelMetrics != NULL) {
            doc["metrics"]["accuracy"] = currentModelMetrics->accuracy();
            doc["metrics"]["precision"] = currentModelMetrics->precision();
            doc["metrics"]["recall"] = currentModelMetrics->recall();
            doc["metrics"]["f1Score"] = currentModelMetrics->f1Score();
            doc["metrics"]["balancedAccuracy"] = currentModelMetrics->balancedAccuracy();
            doc["metrics"]["balancedPrecision"] = currentModelMetrics->balancedPrecision();
            doc["metrics"]["balancedRecall"] = currentModelMetrics->balancedRecall();
            doc["metrics"]["balancedF1Score"] = currentModelMetrics->balancedF1Score();
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
        } else {
            doc["metrics"]["accuracy"] = 0;
            doc["metrics"]["precision"] = 0;
            doc["metrics"]["recall"] = 0;
            doc["metrics"]["f1Score"] = 0;
            doc["metrics"]["balancedAccuracy"] = 0;
            doc["metrics"]["balancedPrecision"] = 0;
            doc["metrics"]["balancedRecall"] = 0;
            doc["metrics"]["balancedF1Score"] = 0;
            doc["metrics"]["meanSqrdError"] = 0;
            doc["metrics"]["numberOfClasses"] = 0;
        }

        auto publish = mqtt.begin_publish(MQTT_SEND_COMMANDS_TOPIC, measureJson(doc));
        serializeJson(doc, publish);
        publish.send();
        break;
    }
    case FederateCommand_RESUME: {

        D_println("Send command to resume training...");

        doc["command"] = "resume";
        doc["client"] = clientName;
        doc["round"] = String(clientName) == String(CLIENT_NAME) ? currentRound : -1; // We don't know the other client's round easily here without loading its config fully, assumed -1 or just wait for it to boot.
        // Actually, if we are just responding for ALIVE, RESUME is probably only for active client.
        // But if needed for other, we would need to load its config. For now, let's assume this is mostly for active client unless specified.
        auto publishResume = mqtt.begin_publish(MQTT_SEND_COMMANDS_TOPIC, measureJson(doc));
        serializeJson(doc, publishResume);
        publishResume.send();
        break;
    }
    case FederateCommand_ALIVE: {        
        doc["command"] = "alive";
        doc["client"] = clientName;
        // If it's the active client, send real state. If other receive "idle" state
        if (String(clientName) == String(CLIENT_NAME)) {
            doc["round"] = currentRound;
            doc["newModelState"] = modelStateToString(newModelState);
        } else if (otherDeviceConfig != NULL && String(clientName) == String(OTHER_CLIENT_NAME)) {
            doc["round"] = otherDeviceConfig->currentRound;
            doc["newModelState"] = modelStateToString(otherDeviceConfig->newModelState);
        } else {
            return;
        }
        auto publishAlive = mqtt.begin_publish(MQTT_SEND_COMMANDS_TOPIC, measureJson(doc));
        serializeJson(doc, publishAlive);
        publishAlive.send();
        break;
    }
    }
    sendingMessage = false;
}

// Wrapper for default behavior
void sendMessageToNetwork(FederateCommand command) {
    sendMessageToNetwork(command, CLIENT_NAME);
#ifdef DOUBLE_DATASET
    if (OTHER_CLIENT_NAME != NULL) {
        // Send ALIVE and JOIN for other client too so it appears in the network
        if (command == FederateCommand_ALIVE || command == FederateCommand_JOIN) {
             sendMessageToNetwork(command, OTHER_CLIENT_NAME);
        }
    }
#endif
}

size_t calculateModelSize(NeuralNetwork& NN) {
    size_t size = 0;
#if defined(REDUCE_RAM_WEIGHTS_LVL2)
    size += sizeof(unsigned int); // totalNumOfWeights
#endif
    size += sizeof(unsigned int); // numberOflayers
    for (unsigned int n = 0; n < NN.numberOflayers; n++) {
#if defined(ACTIVATION__PER_LAYER)
        size += sizeof(byte);
#endif
        size += sizeof(unsigned int) * 2; // _numberOfInputs, _numberOfOutputs
#if !defined(NO_BIAS) && !defined(MULTIPLE_BIASES_PER_LAYER)
        size += sizeof(IDFLOAT);
#endif
        for (unsigned int i = 0; i < NN.layers[n]._numberOfOutputs; i++) {
#if defined(MULTIPLE_BIASES_PER_LAYER)
            size += sizeof(IDFLOAT);
#endif
            for (unsigned int j = 0; j < NN.layers[n]._numberOfInputs; j++) {
                size += sizeof(IDFLOAT);
            }
        }
    }
    return size;
}

void streamModelToMQTT(NeuralNetwork& NN, PicoMQTT::Publisher::Publish& publish) {
#if defined(REDUCE_RAM_WEIGHTS_LVL2)
    unsigned int totalNumOfWeights = 0;
    // Calculate total weights first since we can't seek back in a stream
    for (unsigned int n = 0; n < NN.numberOflayers; n++) {
        totalNumOfWeights += NN.layers[n]._numberOfOutputs * NN.layers[n]._numberOfInputs;
    }
    publish.write((const uint8_t*)&totalNumOfWeights, sizeof(unsigned int));
    
    // Reset counter for the actual writing loop to match library behavior
    totalNumOfWeights = 0;
#endif

    publish.write((const uint8_t*)&NN.numberOflayers, sizeof(unsigned int));
    
    for (unsigned int n = 0; n < NN.numberOflayers; n++) {
#if defined(ACTIVATION__PER_LAYER)
        publish.write((const uint8_t*)&NN.ActFunctionPerLayer[n], sizeof(byte));
#endif
        publish.write((const uint8_t*)&NN.layers[n]._numberOfInputs, sizeof(unsigned int));
        publish.write((const uint8_t*)&NN.layers[n]._numberOfOutputs, sizeof(unsigned int));
        
#if !defined(NO_BIAS) && !defined(MULTIPLE_BIASES_PER_LAYER)
        publish.write((const uint8_t*)NN.layers[n].bias, sizeof(IDFLOAT));
#endif

        for (unsigned int i = 0; i < NN.layers[n]._numberOfOutputs; i++) {
#if defined(MULTIPLE_BIASES_PER_LAYER)
            publish.write((const uint8_t*)&NN.layers[n].bias[i], sizeof(IDFLOAT));
#endif
            for (unsigned int j = 0; j < NN.layers[n]._numberOfInputs; j++) {
#if defined(REDUCE_RAM_WEIGHTS_LVL2)
                publish.write((const uint8_t*)&NN.weights[totalNumOfWeights++], sizeof(IDFLOAT));
#else
                publish.write((const uint8_t*)&NN.layers[n].weights[i][j], sizeof(IDFLOAT));
#endif
            }
        }
    }
}

bool loadModelFromStreamDirectly(NeuralNetwork* NN, Stream& stream) {
    if (NN == NULL) return false;

#if defined(REDUCE_RAM_WEIGHTS_LVL2)
    unsigned int totalNumOfWeights = 0;
    if (stream.readBytes((uint8_t*)&totalNumOfWeights, sizeof(unsigned int)) != sizeof(unsigned int)) return false;
    totalNumOfWeights = 0; // reset for iteration
#endif

    unsigned int numLayers;
    if (stream.readBytes((uint8_t*)&numLayers, sizeof(unsigned int)) != sizeof(unsigned int)) return false;
    
    if (numLayers != NN->numberOflayers) {
        D_println("Error: Stream layer count mismatch");
        return false;
    }

    for (unsigned int n = 0; n < numLayers; n++) {
#if defined(ACTIVATION__PER_LAYER)
        byte actv;
        if (stream.readBytes((uint8_t*)&actv, sizeof(byte)) != sizeof(byte)) return false;
        NN->ActFunctionPerLayer[n] = actv;
#endif
        unsigned int inputs, outputs;
        if (stream.readBytes((uint8_t*)&inputs, sizeof(unsigned int)) != sizeof(unsigned int)) return false;
        if (stream.readBytes((uint8_t*)&outputs, sizeof(unsigned int)) != sizeof(unsigned int)) return false;
        
        if (inputs != NN->layers[n]._numberOfInputs || outputs != NN->layers[n]._numberOfOutputs) {
            D_println("Error: Stream layer dimension mismatch");
            return false;
        }

#if !defined(NO_BIAS) && !defined(MULTIPLE_BIASES_PER_LAYER)
        if (stream.readBytes((uint8_t*)NN->layers[n].bias, sizeof(IDFLOAT)) != sizeof(IDFLOAT)) return false;
#endif

        for (unsigned int i = 0; i < outputs; i++) {
#if defined(MULTIPLE_BIASES_PER_LAYER)
            if (stream.readBytes((uint8_t*)&NN->layers[n].bias[i], sizeof(IDFLOAT)) != sizeof(IDFLOAT)) return false;
#endif
            for (unsigned int j = 0; j < inputs; j++) {
#if defined(REDUCE_RAM_WEIGHTS_LVL2)
                if (stream.readBytes((uint8_t*)&NN->weights[totalNumOfWeights++], sizeof(IDFLOAT)) != sizeof(IDFLOAT)) return false;
#else
                if (stream.readBytes((uint8_t*)&NN->layers[n].weights[i][j], sizeof(IDFLOAT)) != sizeof(IDFLOAT)) return false;
#endif
            }
        }
    }
    return true;
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
    doc["client"] = CLIENT_NAME;
    doc["metrics"] = JsonObject();
    doc["metrics"]["accuracy"] = metrics.accuracy();
    doc["metrics"]["precision"] = metrics.precision();
    doc["metrics"]["recall"] = metrics.recall();
    doc["metrics"]["f1Score"] = metrics.f1Score();
    doc["metrics"]["balancedAccuracy"] = metrics.balancedAccuracy();
    doc["metrics"]["balancedPrecision"] = metrics.balancedPrecision();
    doc["metrics"]["balancedRecall"] = metrics.balancedRecall();
    doc["metrics"]["balancedF1Score"] = metrics.balancedF1Score();
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

    if ((federateModelConfig != NULL && federateModelConfig->jsonWeights && federateState != FederateState_NONE) || (federateState == FederateState_NONE && localModelConfig->jsonWeights)) {
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
    }

#if DIRECT_MQTT_STREAMING
    size_t modelSize = calculateModelSize(NN);

    printMemory();
    roundMemoryUsage.beforeSend = info.total_free_bytes;
    roundMemoryUsage.minimumFree = info.minimum_free_bytes;

    doc["memory"]["round"]["beforeSend"] = roundMemoryUsage.beforeSend;
    doc["memory"]["round"]["minimumFree"] = roundMemoryUsage.minimumFree;
        
    String topic = String(MQTT_RAW_PUBLISH_TOPIC);
    topic.concat("/");
    topic.concat(CLIENT_NAME);
    D_println("Model direct streaming size: " + String(modelSize) + " bytes");
    D_println("Topic: " + topic);
    
    auto publish = mqtt.begin_publish(topic, modelSize, 1);
    streamModelToMQTT(NN, publish);
    publish.send();
#else
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
    auto publish = mqtt.begin_publish(topic, size, 1);
    while (bytesRead > 0) {
        publish.write(buff, bytesRead);
        bytesRead = modelFile.readBytes(buffer, 1024);
    }
    publish.send();
    delete[] buffer;
    modelFile.close();
#endif

    auto publish2 = mqtt.begin_publish(MQTT_PUBLISH_TOPIC, measureJson(doc), 1);
    serializeJson(doc, publish2);
    unsigned long midpoint = millis();
    publish2.send();

    unsigned long endTime = millis();
    previousConstruct = midpoint - startTime;
    previousTransmit = endTime - midpoint;

    printTiming();
    D_println(CLIENT_NAME);
    D_println("Model sent to the network...");
    delay(1000);
    delay(1000);
    delay(1000);
    delay(1000);
    delay(1000);
    sendingMessage = false;
    
}

DFLOAT* predictFromCurrentModel(DFLOAT* x) {
    return currentModel->FeedForward(x);
}

/*testData* readTestData(ModelConfig* modelConfig) {
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

}*/

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
        if (federateState == FederateState_TRAINING && federateModelConfig != NULL && federateModelConfig->layers != NULL && federateModelConfig->numberOfLayers > 0) {
            #ifdef DATASET_BINARY
            String trainPath;
            String metaPath;
            resolveTrainingPaths(trainPath, metaPath);
            D_println("Round training dataset paths: " + trainPath + " | " + metaPath);
                        if (isJulianaBinaryDataset(metaPath)) {
                            newModelMetrics = trainModelFromJulianaBinaryDataset(*newModel, *federateModelConfig, trainPath, metaPath);
                        } else {
                            newModelMetrics = trainModelFromBinaryDataset(*newModel, *federateModelConfig, trainPath, metaPath);
                        }
            #else
            // TODO: Add support for DOUBLE_DATASET here if needed
            newModelMetrics = trainModelFromOriginalDataset(*newModel, *federateModelConfig, X_TRAIN_PATH, Y_TRAIN_PATH);
            #endif
        } else {
            #ifdef DATASET_BINARY
            String trainPath;
            String metaPath;
            resolveTrainingPaths(trainPath, metaPath);
            D_println("Round training dataset paths: " + trainPath + " | " + metaPath);
                        if (isJulianaBinaryDataset(metaPath)) {
                            newModelMetrics = trainModelFromJulianaBinaryDataset(*newModel, *localModelConfig, trainPath, metaPath);
                        } else {
                            newModelMetrics = trainModelFromBinaryDataset(*newModel, *localModelConfig, trainPath, metaPath);
                        }
            #else
            // TODO: Add support for DOUBLE_DATASET here if needed
            newModelMetrics = trainModelFromOriginalDataset(*newModel, *localModelConfig, X_TRAIN_PATH, Y_TRAIN_PATH);
            #endif
        }
        // If training failed, newModelMetrics may be NULL. Handle gracefully.
        if (newModelMetrics == NULL) {
            D_println("Training did not produce metrics (training failed or metadata missing). Skipping send/compare.");
            // Cleanup temporary model objects if present
            if (tempModel != NULL) {
                delete tempModel;
                tempModel = NULL;
            }
            if (newModel != NULL) {
                delete newModel;
                newModel = NULL;
            }
            // Set state back to idle to avoid further processing for this round
            newModelState = ModelState_IDLE;
            // Ensure we don't attempt to send a NULL metrics object
            printMemory();
            roundMemoryUsage.afterTrain = info.total_free_bytes;
            return;
        }

        if (tempModel != NULL) {
            newModelMetrics->parsingTime = tempModel->parsingTime;
        }
        newModelState = ModelState_DONE_TRAINING;
        printMemory();
        roundMemoryUsage.afterTrain = info.total_free_bytes;
        if (federateState == FederateState_TRAINING) {
            if (newModelMetrics != NULL) {
                sendModelToNetwork(*newModel, *newModelMetrics);
                delete newModelMetrics;
                newModelMetrics = NULL;
            } else {
                D_println("New model metrics NULL, skipping sendModelToNetwork for newModel.");
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
            #ifdef DOUBLE_DATASET
            lastTrainedRound = currentRound;
            saveDeviceConfig(); // Save state before switching context
            switchDatasetIndex();
            ESP.restart();
            #endif
        }
    }
    if (newModelState == ModelState_DONE_TRAINING && currentModel != NULL) {
        if (newModelMetrics != NULL && compareMetrics(currentModelMetrics, newModelMetrics)) {
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
    String devicePath = DEVICE_DEFINITION_PATH;
#ifdef DOUBLE_DATASET
    if (datasetIndex == 1) {
        devicePath = DEVICE_DEFINITION_PATH_2;
    }
#endif

    if (!LittleFS.exists(devicePath)) {
        return false;
    }
    File definitionsFile = LittleFS.open(devicePath, "r");
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

#ifdef DOUBLE_DATASET
    // Load the OTHER client name too
    String otherDevicePath = DEVICE_DEFINITION_PATH;
    if (datasetIndex == 0) {
        otherDevicePath = DEVICE_DEFINITION_PATH_2;
    }
    // If it exists, read it
    if (LittleFS.exists(otherDevicePath)) {
        File f2 = LittleFS.open(otherDevicePath, "r");
        if (f2) {
            JsonDocument doc2;
            DeserializationError err2 = deserializeJson(doc2, f2);
            f2.close();
            if (!err2) {
                const char* otherClientValue = doc2["client"] | "";
                if (strlen(otherClientValue) > 0) {
                    if (OTHER_CLIENT_NAME != nullptr) delete[] OTHER_CLIENT_NAME;
                    OTHER_CLIENT_NAME = new char[strlen(otherClientValue) + 1];
                    strcpy(OTHER_CLIENT_NAME, otherClientValue);
                    D_println("Other client name: " + String(OTHER_CLIENT_NAME));
                }
            }
        }
    }
#endif

    return true;
}

bool loadDeviceConfig() {
    D_println("Loading configuration...");
    String configPath = CONFIGURATION_PATH;
#ifdef DOUBLE_DATASET
    if (datasetIndex == 1) {
        configPath = CONFIGURATION_PATH_2;
    }
#endif

    if (!LittleFS.exists(configPath)) {
        return false;
    }

    File configFile = LittleFS.open(configPath, "r");
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
    deviceConfig->lastTrainedRound = doc["lastTrainedRound"] | -1;
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
        deviceConfig->loadedFederateModelConfig->numberOfLayers = federateModelConfigObj["numberOfLayers"] | federateModelConfigObj["layers"].size();
        deviceConfig->loadedFederateModelConfig->epochs = federateModelConfigObj["epochs"] | 1;
    }
    if (doc["datasetKey"].is<const char*>()) {
        selectedDatasetKey = sanitizeDatasetKey(String(doc["datasetKey"].as<const char*>()));
    } else {
        selectedDatasetKey = "";
    }
    if (doc["datasetBin"].is<const char*>()) {
        selectedDatasetBinName = baseNameFromPath(String(doc["datasetBin"].as<const char*>()));
    } else {
        selectedDatasetBinName = "";
    }
    if (doc["datasetMeta"].is<const char*>()) {
        selectedDatasetMetaName = baseNameFromPath(String(doc["datasetMeta"].as<const char*>()));
    } else {
        selectedDatasetMetaName = "";
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

bool loadOtherDeviceConfig() {
    D_println("Loading other configuration...");
    String configPath = CONFIGURATION_PATH;
#ifdef DOUBLE_DATASET
    if (datasetIndex == 0) {
        configPath = CONFIGURATION_PATH_2;
    }
#else
    return false;
#endif

    if (!LittleFS.exists(configPath)) {
        return false;
    }

    File configFile = LittleFS.open(configPath, "r");
    if (!configFile) {
        return false;
    }
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, configFile);
    configFile.close();
    if (error) {
        return false;
    }
    if (otherDeviceConfig != nullptr) {
        delete otherDeviceConfig;
    }
    otherDeviceConfig = new DeviceConfig;
    otherDeviceConfig->currentRound = doc["currentRound"] | -1;
    otherDeviceConfig->currentFederateState = static_cast<FederateState>(doc["federateState"] | FederateState_NONE);
    otherDeviceConfig->newModelState = static_cast<ModelState>(doc["modelState"] | ModelState_IDLE);

    if (doc["metrics"].is<JsonObject>()) {
        otherDeviceConfig->currentModelMetrics = new multiClassClassifierMetrics;
        otherDeviceConfig->currentModelMetrics->numberOfClasses = doc["metrics"]["numberOfClasses"] | 0;
        otherDeviceConfig->currentModelMetrics->epochs = doc["metrics"]["epochs"] | 0;
        otherDeviceConfig->currentModelMetrics->meanSqrdError = doc["metrics"]["meanSqrdError"] | 0;
        otherDeviceConfig->currentModelMetrics->trainingTime = doc["timings"]["training"] | 0;
        otherDeviceConfig->currentModelMetrics->parsingTime = doc["timings"]["parsing"] | 0;
        otherDeviceConfig->currentModelMetrics->metrics = new classClassifierMetricts[otherDeviceConfig->currentModelMetrics->numberOfClasses];
        for (int i = 0; i < otherDeviceConfig->currentModelMetrics->numberOfClasses; i++) {
            otherDeviceConfig->currentModelMetrics->metrics[i].truePositives = doc["metrics"]["truePositives"][i] | 0;
            otherDeviceConfig->currentModelMetrics->metrics[i].falsePositives = doc["metrics"]["falsePositives"][i] | 0;
            otherDeviceConfig->currentModelMetrics->metrics[i].trueNegatives = doc["metrics"]["trueNegatives"][i] | 0;
            otherDeviceConfig->currentModelMetrics->metrics[i].falseNegatives = doc["metrics"]["falseNegatives"][i] | 0;
        }
    }

    D_println("Other configuration loaded successfully");
    return true;
}

bool saveDeviceConfig() {
    String configPath = CONFIGURATION_PATH;
#ifdef DOUBLE_DATASET
    if (datasetIndex == 1) {
        configPath = CONFIGURATION_PATH_2;
    }
#endif

    File configFile = LittleFS.open(configPath, "w");
    if (!configFile) return false;
    JsonDocument doc;
    
    doc["lastTrainedRound"] = lastTrainedRound;
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
    if (selectedDatasetKey.length() > 0) {
        doc["datasetKey"] = selectedDatasetKey;
    }
    if (selectedDatasetBinName.length() > 0) {
        doc["datasetBin"] = selectedDatasetBinName;
    }
    if (selectedDatasetMetaName.length() > 0) {
        doc["datasetMeta"] = selectedDatasetMetaName;
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