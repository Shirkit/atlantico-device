#define NumberOf(arg) ((unsigned int)(sizeof(arg) / sizeof(arg[0]))) // calculates the number of layers (in this case 4)

#define _1_OPTIMIZE 0B00000001 // USE_64_BIT_DOUBLE
#define _2_OPTIMIZE 0B00100000 // MULTIPLE_BIASES_PER_LAYER

#define DEBUG 1    // SET TO 0 OUT TO REMOVE TRACES

#define PARALLEL
#define ACTIVATION__PER_LAYER // DEFAULT KEYWORD for allowing the use of any Activation-Function per "Layer-to-Layer".
// #define Sigmoid
// #define Tanh
#define ReLU
#define Softmax
// #define LeakyReLU
// #define ELU
// #define SELU

//#include <NeuralNetwork.h>
//#include <SPIFFS.h>
#include "ModelUtil.cpp"

void printInstructions() {
  Serial.println("Choose an option to coninue:");
  Serial.println("1. Print Model");
  Serial.println("2. Train Model");
  Serial.println("3. Save Model");
  Serial.println("4. Load Model");
  Serial.println("5. Send Model to Network");
  Serial.println("6. Print Model Metrics");
  Serial.println("7. Read test Data and Predict from Current Model");
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
    delay(100); // Avoids blocking the loop
  }
}

void setup()
{
  Serial.begin(115200);
  randomSeed(10);
  unsigned int layers[] = { 20, 16, 8, 6 };
  byte Actv_Functions[] = { 0, 0, 1 };
  bootUp(layers, NumberOf(layers), Actv_Functions, 0, 0);
  printInstructions();

  #ifdef PARALLEL
  xTaskCreatePinnedToCore(
    processIncomingMessages, /* Function to implement the task */
    "Background Message Processing Task", /* Name of the task */
    40000 , /* Stack size in bytes */
    NULL, /* Task input parameter */
    1, /* Priority of the task */
    NULL, /* Task handle. */
    0); /* Core where the task should run */
  // xTaskCreate(processIncomingMessages, "Background Message Processing Task", 2000, NULL, 1, NULL);
  // xSemaphoreCurrentModel = xSemaphoreCreateMutex();
  #endif
}

void loop()
{
  #ifndef PARALLEL
  processMessages();
  #else
  if (newModelState == DONE_TRAINING) {
    // ! TODO needs to check if new metrics are actually better than the current model ones
    if (true) {
      delete currentModel;
      currentModel = newModel;
      newModel = NULL;
      newModelState = IDLE;
      if (currentModelMetrics != NULL) {
        delete currentModelMetrics;
      }
      currentModelMetrics = newModelMetrics;
      newModelMetrics = NULL;
    }
  }
  if (newModelState == READY_TO_TRAIN) {
    if (newModelMetrics != NULL) {
      delete newModelMetrics;
    }
    newModelState = MODEL_BUSY;
    newModelMetrics = trainModelFromOriginalDataset(*newModel, X_TRAIN_PATH, Y_TRAIN_PATH);
    newModelState = DONE_TRAINING;
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
      sendModelToNetwork(*currentModel);
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
    case 9:
    {
      SPIFFS.exists(MODEL_PATH) ? SPIFFS.remove(MODEL_PATH) : Serial.println("Model not found");
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
      sendModelToNetwork(*currentModel);
      break;
    case 16:
      newModelMetrics->print();
      break;
    case 19:
      SPIFFS.exists(NEW_MODEL_PATH) ? SPIFFS.remove(NEW_MODEL_PATH) : Serial.println("Model not found");
      break;
    case 99:
      printInstructions();
      break;
    default:
      break;
    }
  }
}