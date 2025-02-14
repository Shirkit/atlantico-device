/*
Esse arquivo demonstra o processo de leitura de um arquivo CSV com os valores de entrada e saída esperados para o treinamento de uma rede neural.
*/

#define NumberOf(arg) ((unsigned int)(sizeof(arg) / sizeof(arg[0]))) // calculates the number of layers (in this case 4)

#define _1_OPTIMIZE 0B00000001 // Highly-Recommended Optimization For RAM
#define _2_OPTIMIZE 0B00100000 // MULTIPLE_BIASES_PER_LAYER

#define ACTIVATION__PER_LAYER // DEFAULT KEYWORD for allowing the use of any Activation-Function per "Layer-to-Layer".
#define ReLU                  // 0
#define Softmax               // 1

#include <NeuralNetwork.h>
#include "SPIFFS.h"

unsigned int layers[] = {20, 16, 8, 6}; // 4 layers (1st)layer with 3 input neurons (2nd & 3rd)layer 9 hidden neurons each and (4th)layer with 1 output neuron
byte Actv_Functions[] = {0, 0, 1};      // 0 = ReLU, 1 = Softmax
double *output; // 4th layer's output

void printArray(double arr[])
{
  Serial.print("Array: [");
  for (int i = 0; i < NumberOf(arr); i++)
  {
    Serial.print(arr[i]);
    Serial.print(", ");
  }
  Serial.println("]");
}

void setup()
{
  NeuralNetwork NN(layers, NumberOf(layers), Actv_Functions); // Creating a NeuralNetwork with pretrained Weights and Biases

  Serial.begin(115200);
  delay(1000);
  Serial.println("Starting...");
  delay(1000);
  if (!SPIFFS.begin(true))
  {
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }

  File file = SPIFFS.open("/x_train_esp32.csv", "r");
  if (!file)
  {
    Serial.println("Failed to open file for reading");
    return;
  }

  File yfile = SPIFFS.open("/y_train_esp32.csv", "r");
  if (!yfile)
  {
    Serial.println("Failed to open file for reading");
    return;
  }

  Serial.println("Reading from file:");
  char str[1024];
  char *values;
  double val;
  double x[layers[0]], y[layers[0]];
  while (file.available() && yfile.available())
  {
    size_t bytes_read = file.readBytesUntil('\n', str, 1023);
    str[bytes_read] = '\0';

    values = strtok(str, ",");
    int j = 0;
    while (values != NULL)
    {
      val = strtod(values, NULL);
      x[j % layers[0]] = val;
      j++;
      if (j % layers[0] == 0)
        j = 0;
      values = strtok(NULL, ",");
    }

    bytes_read = yfile.readBytesUntil('\n', str, 1023);
    str[bytes_read] = '\0';

    values = strtok(str, ",");
    j = 0;
    while (values != NULL)
    {
      val = strtod(values, NULL);
      y[j % layers[0]] = val;
      j++;
      if (j % layers[0] == 0)
        j = 0;
      values = strtok(NULL, ",");
    }

    //printArray(x);
    printArray(y);

    NN.FeedForward(x);
    NN.BackProp(y);
    NN.getMeanSqrdError(1);

    // printArray(x);
  }
  file.close();
  Serial.println("Done");

  NN.print();
}

void loop()
{
}