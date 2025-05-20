#define D_printf(...)   Serial.printf(__VA_ARGS__)

#include <Arduino.h>
#include <esp_heap_caps.h>

multi_heap_info_t info;

void printMemory() {
  heap_caps_get_info(&info, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT); // internal RAM, memory capable to store data or to create new task
  D_printf("Heap info: %d bytes free\n", info.total_free_bytes);
  D_printf("Heap info: %d bytes largest free block\n", info.largest_free_block);
  D_printf("Heap info: %d bytes minimum free ever\n", info.minimum_free_bytes);
}

void setup() {
  Serial.begin(115200);
  printMemory();
}

void loop() {
  printMemory();
}