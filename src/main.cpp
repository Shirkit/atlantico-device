#define NumberOf(arg) ((unsigned int)(sizeof(arg) / sizeof(arg[0]))) // calculates the number of layers (in this case 4)

// #define _1_OPTIMIZE 0B00000001 // USE_64_BIT_DOUBLE
#define _2_OPTIMIZE 0B00100000 // MULTIPLE_BIASES_PER_LAYER

#define ACTIVATION__PER_LAYER // DEFAULT KEYWORD for allowing the use of any Activation-Function per "Layer-to-Layer".
#define ReLU                  // 0
#define Softmax               // 1

#include "ModelUtil.cpp"

unsigned int layers[] = {20, 16, 8, 6}; // 4 layers (1st)layer with 3 input neurons (2nd & 3rd)layer 9 hidden neurons each and (4th)layer with 1 output neuron
byte Actv_Functions[] = {0, 0, 1};      // 0 = ReLU, 1 = Softmax
double *output; // 4th layer's output

void setup()
{
  Serial.begin(115200);
  bootUp(layers, NumberOf(layers), Actv_Functions);

  // trainModelFromOriginalDataset(*currentModel, X_TRAIN_PATH, Y_TRAIN_PATH);

  // saveModelToFlash(*currentModel, MODEL_PATH);
  Serial.println("Choose an option to coninue:");
  Serial.println("1. Print Model");
  Serial.println("2. Train Model");
  Serial.println("3. Save Model");
  Serial.println("4. Load Model");
  Serial.println("5. Print New Model");
  Serial.println("6. Train New Model");
  Serial.println("7. Save New Model");
  Serial.println("8. Load New Model");
  Serial.println("9. Send Current Model to Network");
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
      break;
    case 4:
      break;
    case 5:
      newModel->print();
      break;
    case 6:
      trainModelFromOriginalDataset(*newModel, X_TRAIN_PATH, Y_TRAIN_PATH);
      break;
    case 7:
      break;
    case 8:
      break;
    case 9:
      break;
    default:
      break;
    }
  }
}
