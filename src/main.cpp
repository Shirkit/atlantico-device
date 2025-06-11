#define NumberOf(arg) ((unsigned int)(sizeof(arg) / sizeof(arg[0])))

// #define _1_OPTIMIZE 0B00000001
#define _2_OPTIMIZE 0B00100000

#define DEBUG 1 // SET TO 0 OUT TO REMOVE TRACES

#define ACTIVATION__PER_LAYER
#define Sigmoid //[default] No need definition, for single activation across network
#define Tanh
#define ReLU
#define LeakyReLU
#define ELU
#define SELU
#define Softmax

#include "ModelUtil.cpp"
#include <esp_task_wdt.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

void printInstructions() {
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
  Serial.println("10. Delete Configuration");
  Serial.println("11. Print New Model");
  Serial.println("12. Train New Model");
  Serial.println("13. Save New Model");
  Serial.println("14. Load New Model");
  Serial.println("15. Send Model to Network");
  Serial.println("16. Print New Model Metrics");
  Serial.println("19. Delete New Model");
  Serial.println("99. Print these Instructions");
}

void parseSerial() {
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
      currentModelMetrics = trainModelFromOriginalDataset(*currentModel, *localModelConfig, X_TRAIN_PATH, Y_TRAIN_PATH);
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
      testData* test = readTestData(localModelConfig);
      if (test != NULL) {
        IDFLOAT* prediction = predictFromCurrentModel(test->x);
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
    case 10:
      LittleFS.exists(CONFIGURATION_PATH) ? LittleFS.remove(CONFIGURATION_PATH) : Serial.println("Configuration not found");
      break;
    case 11:
      newModel->print();
      break;
    case 12:
      if (newModelMetrics != NULL) {
        delete newModelMetrics;
      }
      newModelMetrics = trainModelFromOriginalDataset(*newModel, *localModelConfig, X_TRAIN_PATH, Y_TRAIN_PATH);
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
    case 20:
      federateState = FederateState_NONE;
      currentRound = -1;
      newModelState = ModelState_IDLE;
      saveDeviceConfig();
      break;
    case 99:
      printInstructions();
      break;
    default:
      break;
    }
  }
}

void processIncomingMessages(void *pvParameters) {
  while (true) {
    processMessages();
    vTaskDelay(100 / portTICK_PERIOD_MS); // Yeild to other OS tasks
  }
}

void setup()
{
  Serial.begin(115200);
  printMemory();
  fixedMemoryUsage.onBoot = info.total_free_bytes;
  unsigned int* layers = new unsigned int[5] { 3, 40, 20, 10, 6 };
  byte* Actv_Functions = new byte[4] { 1,  1,  1,  6 };
  
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); //disable brownout detector

  localModelConfig = new ModelConfig(layers, 5, Actv_Functions, 1, 10, 0.3333f / 12.0f, 0.06666f / 12.0f);
  
  randomSeed(localModelConfig->randomSeed);
  bootUp(false);
  printMemory();
  printInstructions();

  esp_task_wdt_init(30000, true);

  xTaskCreatePinnedToCore(
    processIncomingMessages, /* Function to implement the task */
    "Atlantico Device", /* Name of the task */
    5000 , /* Stack size in bytes */
    NULL, /* Task input parameter */
    1, /* Priority of the task */
    NULL, /* Task handle. */
    0); /* Core where the task should run, 0 is the OS, 1 is the app */
  // xTaskCreate(processIncomingMessages, "Background Message Processing Task", 2000, NULL, 1, NULL);
  // xSemaphoreCurrentModel = xSemaphoreCreateMutex();

  printMemory();

  fixedMemoryUsage.afterFullSetup = info.total_free_bytes;
  fixedMemoryUsage.minFreeHeapAfterSetup = info.minimum_free_bytes;

  D_println(fixedMemoryUsage.onBoot);
  D_println(fixedMemoryUsage.loadConfig);
  D_println(fixedMemoryUsage.loadAndTrainModel);
  D_println(fixedMemoryUsage.connectionMade);
  D_println(fixedMemoryUsage.afterFullSetup);
  D_println(fixedMemoryUsage.minFreeHeapAfterSetup);
}

void loop()
{
  processModel();

  parseSerial();
}