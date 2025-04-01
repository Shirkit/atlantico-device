#include <Arduino.h>

void TaskCode(void *pvParameters);

void setup() {
  Serial.begin(115200);
  delay(1000);

  uint8_t taskNumber1 = 1;
  xTaskCreate(TaskCode, "Task1", 2000, &taskNumber1, 2, NULL);
  //delay(5);

  uint8_t taskNumber2 = 2;
  xTaskCreate(TaskCode, "Task2", 2000, &taskNumber2, 2, NULL);
  //delay(5);

  uint8_t taskNumber3 = 3;
  xTaskCreate(TaskCode, "Task3", 2000, &taskNumber3, 2, NULL);
  //delay(5);

  uint8_t taskNumber4 = 4;
  xTaskCreate(TaskCode, "Task4", 2000, &taskNumber4, 2, NULL);
  delay(5);
}

void TaskCode(void *pvParameters) {
  uint8_t taskNumber = *static_cast<uint8_t *>(pvParameters);
  Serial.printf("---------------------------- Task #%d Started on Core %d -------------------------\n", taskNumber, xPortGetCoreID());
  delay(taskNumber * 2000); // Simulate some work
  Serial.printf("---------------------------- Task #%d Terminated on Core %d ---------------------------------\n", taskNumber, xPortGetCoreID());
  vTaskDelete(NULL);
}

void loop() {
  delay(1);
}