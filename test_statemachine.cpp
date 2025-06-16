#include "StateMachine.h"
#include <Arduino.h>

// Simple test function to verify state machine behavior
void testStateMachine() {
    Serial.println("=== StateMachine Test ===");
    
    StateMachine sm;
    
    // Test initial state
    Serial.print("Initial state: ");
    Serial.println(sm.getStateString());
    sm.printCurrentState();
    
    // Test valid transition: INITIALIZING -> INFERENCE_MODE
    Serial.println("\nTesting INITIALIZING -> INFERENCE_MODE...");
    if (sm.transitionTo(INFERENCE_MODE)) {
        Serial.println("✓ Transition successful");
    } else {
        Serial.println("✗ Transition failed");
    }
    sm.printCurrentState();
    
    // Test federation status changes
    Serial.println("\nTesting federation status: NONE -> SUBSCRIBED...");
    if (sm.setFederateStatus(FEDERATE_SUBSCRIBED)) {
        Serial.println("✓ Status change successful");
    } else {
        Serial.println("✗ Status change failed");
    }
    
    // Test starting federation
    Serial.println("\nTesting INFERENCE_MODE -> FEDERATION_TRAINING...");
    if (sm.canStartFederation()) {
        Serial.println("✓ Can start federation");
        if (sm.transitionTo(FEDERATION_TRAINING)) {
            Serial.println("✓ Federation training started");
        }
    }
    sm.printCurrentState();
    
    // Test invalid transition
    Serial.println("\nTesting invalid transition: FEDERATION_TRAINING -> DEVICE_INITIALIZING...");
    if (sm.transitionTo(DEVICE_INITIALIZING)) {
        Serial.println("✗ Invalid transition allowed!");
    } else {
        Serial.println("✓ Invalid transition correctly rejected");
    }
    
    // Test policy methods
    Serial.println("\nTesting policy methods...");
    Serial.print("Federation has priority: ");
    Serial.println(StateMachine::federationHasPriority(sm.getCurrentState()) ? "Yes" : "No");
    
    Serial.print("Requires model unload: ");
    Serial.println(StateMachine::requiresModelUnload(sm.getCurrentState()) ? "Yes" : "No");
    
    Serial.print("Allows sensor operation: ");
    Serial.println(StateMachine::allowsSensorOperation(sm.getCurrentState()) ? "Yes" : "No");
    
    // Test error handling
    Serial.println("\nTesting error handling...");
    sm.reportError();
    sm.reportError();
    Serial.print("Should retry after error: ");
    Serial.println(sm.shouldRetryAfterError() ? "Yes" : "No");
    
    sm.reportError(); // Third error should trigger DEVICE_ERROR state
    Serial.print("In error state: ");
    Serial.println(sm.isInErrorState() ? "Yes" : "No");
    
    sm.printCurrentState();
    sm.printTransitionHistory();
    
    Serial.println("=== StateMachine Test Complete ===");
}
