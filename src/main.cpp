#define NumberOf(arg) ((unsigned int)(sizeof(arg) / sizeof(arg[0]))) // calculates the number of layers (in this case 4)

#define _1_OPTIMIZE 0B00000001 // USE_64_BIT_DOUBLE
#define _2_OPTIMIZE 0B00100000 // MULTIPLE_BIASES_PER_LAYER

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

unsigned int layers[] = { 20, 16, 8, 6 };
byte Actv_Functions[] = { 0, 0, 1 };

void printInstructions() {
  Serial.println("Choose an option to coninue:");
  Serial.println("1. Print Model");
  Serial.println("2. Train Model");
  Serial.println("3. Save Model");
  Serial.println("4. Load Model");
  Serial.println("5. Send Model to Network");
  Serial.println("9. Delete Model");
  Serial.println("11. Print New Model");
  Serial.println("12. Train New Model");
  Serial.println("13. Save New Model");
  Serial.println("14. Load New Model");
  Serial.println("15. Send Model to Network");
  Serial.println("19. Delete New Model");
  Serial.println("99. Print these Instructions");
}

void setup()
{
  Serial.begin(115200);
  randomSeed(10);
  bootUp(layers, NumberOf(layers), Actv_Functions, 0, 0);
  printInstructions();
}

void loop()
{

  if (Serial.available() != 0)
  {
    int option = Serial.parseInt();

    switch (option)
    {
    case 1:
      currentModel->print();
      break;
    case 2:
      trainModelFromOriginalDataset(*currentModel, X_TRAIN_PATH, Y_TRAIN_PATH);
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
      for (int i = 0; i < sizeof(layers); i++) {
      }
      break;
    case 9:
    {
      SPIFFS.exists(MODEL_PATH) ? SPIFFS.remove(MODEL_PATH) : Serial.println("Model not found");
      break;
    }
    case 11:
      newModel->print();
      break;
    case 12:
      trainModelFromOriginalDataset(*newModel, X_TRAIN_PATH, Y_TRAIN_PATH);
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


  processMessages();
}
