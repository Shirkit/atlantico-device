#define NumberOf(arg) ((unsigned int)(sizeof(arg) / sizeof(arg[0]))) // calculates the number of layers (in this case 4)

// #define _1_OPTIMIZE 0B00000001 // USE_64_BIT_DOUBLE
#define _2_OPTIMIZE 0B00100000 // MULTIPLE_BIASES_PER_LAYER

#define DEBUG 1    // SET TO 0 OUT TO REMOVE TRACES

#define PARALLEL
#define ACTIVATION__PER_LAYER // DEFAULT KEYWORD for allowing the use of any Activation-Function per "Layer-to-Layer".
// #define Sigmoid
#define Tanh
// #define ReLU
#define Softmax
// #define LeakyReLU
// #define ELU
// #define SELU

//#include <NeuralNetwork.h>
//#include <SPIFFS.h>
#include "ModelUtil.cpp"
#include <esp_task_wdt.h>

void printInstructions() {
  Serial.println(CLIENT_NAME);
  Serial.println("Choose an option to coninue:");
  Serial.println("1. Print Model");
  Serial.println("2. Train Model");
  Serial.println("3. Save Model");
  Serial.println("4. Load Model");
  Serial.println("5. Send Model to Network");
  Serial.println("6. Print Model Metrics");
  Serial.println("7. Read test Data and Predict from Current Model");
  Serial.println("8. Print Memory usage");
  Serial.println("9. Delete Model");
  Serial.println("11. Print New Model");
  Serial.println("12. Train New Model");
  Serial.println("13. Save New Model");
  Serial.println("14. Load New Model");
  Serial.println("15. Send Model to Network");
  Serial.println("16. Print New Model Metrics");
  Serial.println("19. Delete New Model");
  Serial.println("99. Print these Instructions");
}

void processIncomingMessages(void *pvParameters) {
  while (true) {
    processMessages();
    // taskYIELD(); // Yield to other tasks
    vTaskDelay(100 / portTICK_PERIOD_MS);
    // delay(100); // Avoids blocking the loop
  }
}

void setup()
{
  Serial.begin(115200);
  printMemory();
  randomSeed(10);
  unsigned int layers[] = { 3, 40, 20, 10, 6 };
  byte Actv_Functions[] = { 0,  0,  0,  1 };
  bootUp(layers, NumberOf(layers), Actv_Functions, 0.11f / 8.0f, 0.022f / 8.0f);
  printMemory();
  printInstructions();

  esp_task_wdt_init(10000, true);

  #ifdef PARALLEL
  xTaskCreatePinnedToCore(
    processIncomingMessages, /* Function to implement the task */
    "Atlantico Device", /* Name of the task */
    10000 , /* Stack size in bytes */
    NULL, /* Task input parameter */
    1, /* Priority of the task */
    NULL, /* Task handle. */
    0); /* Core where the task should run */
  // xTaskCreate(processIncomingMessages, "Background Message Processing Task", 2000, NULL, 1, NULL);
  // xSemaphoreCurrentModel = xSemaphoreCreateMutex();
  #endif
  printMemory();
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

void loop()
{
  #ifndef PARALLEL
  processMessages();
  #else
  if (newModelState == ModelState_READY_TO_TRAIN) {
    if (newModelMetrics != NULL) {
      delete newModelMetrics;
    }
    newModelState = ModelState_MODEL_BUSY;
    // ! }It was throwing kernel panic due to high cpu usage without releasing the core before increasing the WatchDog timer
    newModelMetrics = trainModelFromOriginalDataset(*newModel, X_TRAIN_PATH, Y_TRAIN_PATH);
    newModelState = ModelState_DONE_TRAINING;
    if (fedareState == FederateState_TRAINING) {
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
    } else {
      delete newModel;
      newModel = NULL;
      newModelState = ModelState_IDLE;
      if (newModelMetrics != NULL) {
        delete newModelMetrics;
      }
      newModelMetrics = NULL;
    }
    if (fedareState == FederateState_DONE) {
      fedareState = FederateState_SUBSCRIBED;
    }
  }
  #endif

  if (Serial.available() != 0)
  {
    int option = Serial.parseInt();

    switch (option)
    {
    case 1:
      currentModel->print();
      break;
    case 2:
      if (currentModelMetrics != NULL) {
        delete currentModelMetrics;
      }
      currentModelMetrics = trainModelFromOriginalDataset(*currentModel, X_TRAIN_PATH, Y_TRAIN_PATH);
      break;
    case 3:
      saveModelToFlash(*currentModel, MODEL_PATH);
      break;
    case 4:
      currentModel = loadModelFromFlash(MODEL_PATH);
      break;
    case 5:
      sendModelToNetwork(*currentModel, *currentModelMetrics);
      break;
    case 6:
      currentModelMetrics->print();
      break;
    case 7: {
      testData* test = readTestData();
      if (test != NULL) {
        DFLOAT* prediction = predictFromCurrentModel(test->x);
        Serial.print("Prediction: ");
        bool correct = true;
        for (int j = 0; j < currentModel->layers[currentModel->numberOflayers - 1]._numberOfOutputs; j++) {
          Serial.print(prediction[j], 0);
          Serial.print("=");
          Serial.print(test->y[j], 0);
          if (j < currentModel->layers[currentModel->numberOflayers - 1]._numberOfOutputs - 1) {
            Serial.print(" - ");
          }
        }
        Serial.println();
      }
      break;
    }
    case 8:
      printMemory();
      break;
    case 9:
    {
      LittleFS.exists(MODEL_PATH) ? LittleFS.remove(MODEL_PATH) : Serial.println("Model not found");
      break;
    }
    case 11:
      newModel->print();
      break;
    case 12:
      if (newModelMetrics != NULL) {
        delete newModelMetrics;
      }
      newModelMetrics = trainModelFromOriginalDataset(*newModel, X_TRAIN_PATH, Y_TRAIN_PATH);
      break;
    case 13:
      saveModelToFlash(*newModel, NEW_MODEL_PATH);
      break;
    case 14:
      newModel = loadModelFromFlash(NEW_MODEL_PATH);
      break;
    case 15:
      sendModelToNetwork(*newModel, *newModelMetrics);
      break;
    case 16:
      newModelMetrics->print();
      break;
    case 19:
    LittleFS.exists(NEW_MODEL_PATH) ? LittleFS.remove(NEW_MODEL_PATH) : Serial.println("Model not found");
      break;
    case 99:
      printInstructions();
      break;
    default:
      break;
    }
  }
}