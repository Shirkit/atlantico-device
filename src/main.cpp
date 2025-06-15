#include "Config.h"
#include "Types.h"
#include "AtlanticoDevice.h"
#include <esp_task_wdt.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

#if USE_ADVANCED_LOGGER
#include "Logger.h"
#endif

// Global instances
DeviceManager* deviceManager;
SerialInterface* serialInterface;

void processIncomingMessages(void *pvParameters) {
    while (true) {
        deviceManager->processMessages();
        vTaskDelay(100 / portTICK_PERIOD_MS); // Yield to other OS tasks
    }
}

void setup() {
    Serial.begin(57600);
    
    // Initialize logging system
    Logger::init(LOG_LEVEL_DEBUG);
    LOG_INFO("ESP32 Federated Learning Device Starting...");
    
    // Disable brownout detector
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
    
    // Initialize device manager
    deviceManager = new DeviceManager();
    
    // Print initial memory usage and capture onBoot memory
    // deviceManager->printMemory();
    deviceManager->captureOnBootMemory();
    
    // Configure neural network
    unsigned int* layers = new unsigned int[3] { 3, 9, 6 };
    byte* actvFunctions = new byte[2] { 1, 6 };
    
    ModelConfig* localConfig = new ModelConfig(layers, 3, actvFunctions, 1, 10, 0.3333f / 12.0f, 0.06666f / 12.0f);
    deviceManager->getModelManager()->setLocalModelConfig(localConfig);
    
    randomSeed(localConfig->randomSeed);
    
    // Boot up the device (without initializing base model to save memory)
    deviceManager->bootUp(true);
    
    // Initialize serial interface
    serialInterface = new SerialInterface(deviceManager);
    serialInterface->printInstructions();
    
    // Configure watchdog timer
    esp_task_wdt_init(30000, true);
    
    // Capture final memory state after full setup
    // deviceManager->printMemory();
    deviceManager->captureAfterFullSetupMemory();
    
    // Create background task for message processing AFTER bootUp() completes
    xTaskCreatePinnedToCore(
        processIncomingMessages,
        "Atlantico Device",
        5000,
        NULL,
        1,
        NULL,
        0  // Core 0 is for OS, Core 1 is for app
    );
    
    LOG_INFO("Setup complete - device ready for operation");
}

void loop() {
    // Process model state machine
    deviceManager->processModel();
    
    // Handle serial commands
    serialInterface->processSerial();
    
    // Small delay to prevent watchdog issues
    delay(10);
}