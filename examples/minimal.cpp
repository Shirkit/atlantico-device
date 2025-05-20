#define NumberOf(arg) ((unsigned int)(sizeof(arg) / sizeof(arg[0]))) // calculates the number of layers (in this case 4)
#define D_printf(...)   Serial.printf(__VA_ARGS__)
#define _2_OPTIMIZE 0B00100000 // MULTIPLE_BIASES_PER_LAYER
#define ACTIVATION__PER_LAYER // DEFAULT KEYWORD for allowing the use of any Activation-Function per "Layer-to-Layer".
#define Tanh
#define Softmax


#include <Arduino.h>
#include <esp_heap_caps.h>
#include <NeuralNetwork.h>

multi_heap_info_t info;
NeuralNetwork* currentModel = NULL;

void printMemory() {
  heap_caps_get_info(&info, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT); // internal RAM, memory capable to store data or to create new task
  D_printf("Heap info: %d bytes free\n", info.total_free_bytes);
  D_printf("Heap info: %d bytes largest free block\n", info.largest_free_block);
  D_printf("Heap info: %d bytes minimum free ever\n", info.minimum_free_bytes);
}

void setup() {
  Serial.begin(115200);
  printMemory();

  randomSeed(10);
  unsigned int layers[] = { 3, 40, 20, 10, 6 };
  byte Actv_Functions[] = { 0,  0,  0,  1 };
  currentModel = new NeuralNetwork(layers, NumberOf(layers), Actv_Functions);
  printMemory(); 
}

void loop() {
}