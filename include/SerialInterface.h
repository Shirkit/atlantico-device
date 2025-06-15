#ifndef SERIAL_INTERFACE_H_
#define SERIAL_INTERFACE_H_

#include "Config.h"
#include "DeviceManager.h"

class SerialInterface {
private:
    DeviceManager* deviceManager;
    
public:
    SerialInterface(DeviceManager* deviceManager);
    
    void printInstructions();
    void processSerial();
    
private:
    void handleCurrentModelCommands(int option);
    void handleNewModelCommands(int option);
    void handleSystemCommands(int option);
    void handleTestAndPrediction();
};

#endif /* SERIAL_INTERFACE_H_ */
