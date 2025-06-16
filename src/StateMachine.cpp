#include "StateMachine.h"
#include <Arduino.h>

StateMachine::StateMachine() :
    currentState(DEVICE_INITIALIZING),
    federateStatus(FEDERATE_NONE),
    previousState(DEVICE_INITIALIZING),
    stateEntryTime(millis()),
    lastTransitionTime(millis()),
    errorCount(0),
    stateBeforeError(DEVICE_INITIALIZING)
{
    LOG_INFO("StateMachine initialized in DEVICE_INITIALIZING state");
}

bool StateMachine::transitionTo(DeviceState newState) {
    if (!isValidTransition(currentState, newState)) {
        LOG_WARN("Invalid transition from %s to %s", getStateString(), getStateString());
        return false;
    }
    
    previousState = currentState;
    currentState = newState;
    lastTransitionTime = millis();
    stateEntryTime = lastTransitionTime;
    
    // Clear errors on successful transition out of error state
    if (previousState == DEVICE_ERROR && newState != DEVICE_ERROR) {
        clearErrors();
    }
    
    LOG_INFO("State transition: %s", getStateString());
    
    return true;
}

bool StateMachine::setFederateStatus(FederateStatus newStatus) {
    if (!isValidStatusTransition(federateStatus, newStatus)) {
        LOG_WARN("Invalid federation status transition");
        return false;
    }
    
    federateStatus = newStatus;
    
    LOG_INFO("Federation status: %s", getStatusString());
    
    return true;
}

bool StateMachine::isValidTransition(DeviceState from, DeviceState to) const {
    // Allow self-transitions
    if (from == to) return true;
    
    switch (from) {
        case DEVICE_INITIALIZING:
            // Can go to inference mode or error
            return (to == INFERENCE_MODE || to == DEVICE_ERROR);
            
        case INFERENCE_MODE:
            // Can start federation training or enter error state
            return (to == FEDERATION_TRAINING || to == DEVICE_ERROR);
            
        case FEDERATION_TRAINING:
            // Can return to inference, enter recovery, or error
            return (to == INFERENCE_MODE || to == FEDERATION_RECOVERY || to == DEVICE_ERROR);
            
        case FEDERATION_RECOVERY:
            // Can retry training, return to inference, or error permanently
            return (to == FEDERATION_TRAINING || to == INFERENCE_MODE || to == DEVICE_ERROR);
            
        case DEVICE_ERROR:
            // Can recover to any state except initializing (no restarts)
            return (to == INFERENCE_MODE || to == FEDERATION_RECOVERY);
            
        default:
            return false;
    }
}

bool StateMachine::isValidStatusTransition(FederateStatus from, FederateStatus to) const {
    // Allow self-transitions
    if (from == to) return true;
    
    switch (from) {
        case FEDERATE_NONE:
            return (to == FEDERATE_SUBSCRIBED);
            
        case FEDERATE_SUBSCRIBED:
            return (to == FEDERATE_TRAINING || to == FEDERATE_NONE);
            
        case FEDERATE_TRAINING:
            return (to == FEDERATE_ENDING || to == FEDERATE_NONE);
            
        case FEDERATE_ENDING:
            return (to == FEDERATE_SUBSCRIBED || to == FEDERATE_NONE);
            
        default:
            return false;
    }
}

bool StateMachine::isInferenceAllowed() const {
    return (currentState == INFERENCE_MODE && federateStatus != FEDERATE_TRAINING);
}

bool StateMachine::isFederationActive() const {
    return (currentState == FEDERATION_TRAINING || 
            currentState == FEDERATION_RECOVERY ||
            federateStatus == FEDERATE_TRAINING);
}

bool StateMachine::isInitialized() const {
    return (currentState != DEVICE_INITIALIZING);
}

bool StateMachine::isInErrorState() const {
    return (currentState == DEVICE_ERROR);
}

bool StateMachine::canStartFederation() const {
    return (currentState == INFERENCE_MODE && 
            federateStatus == FEDERATE_SUBSCRIBED);
}

bool StateMachine::canStopFederation() const {
    return (currentState == FEDERATION_TRAINING || 
            currentState == FEDERATION_RECOVERY);
}

unsigned long StateMachine::getTimeInCurrentState() const {
    return millis() - stateEntryTime;
}

unsigned long StateMachine::getTimeSinceLastTransition() const {
    return millis() - lastTransitionTime;
}

void StateMachine::reportError() {
    errorCount++;
    if (currentState != DEVICE_ERROR) {
        stateBeforeError = currentState;
    }
    
    if (errorCount >= MAX_ERROR_COUNT) {
        transitionTo(DEVICE_ERROR);
    }
    
    LOG_ERROR("Error reported. Count: %d", errorCount);
}

void StateMachine::clearErrors() {
    errorCount = 0;
    LOG_INFO("Errors cleared");
}

bool StateMachine::shouldRetryAfterError() const {
    return (errorCount < MAX_ERROR_COUNT && currentState != DEVICE_ERROR);
}

const char* StateMachine::getStateString() const {
    switch (currentState) {
        case DEVICE_INITIALIZING:    return "DEVICE_INITIALIZING";
        case INFERENCE_MODE:         return "INFERENCE_MODE";
        case FEDERATION_TRAINING:    return "FEDERATION_TRAINING";
        case FEDERATION_RECOVERY:    return "FEDERATION_RECOVERY";
        case DEVICE_ERROR:          return "DEVICE_ERROR";
        default:                    return "UNKNOWN";
    }
}

const char* StateMachine::getStatusString() const {
    switch (federateStatus) {
        case FEDERATE_NONE:        return "FEDERATE_NONE";
        case FEDERATE_SUBSCRIBED:  return "FEDERATE_SUBSCRIBED";
        case FEDERATE_TRAINING:    return "FEDERATE_TRAINING";
        case FEDERATE_ENDING:      return "FEDERATE_ENDING";
        default:                   return "UNKNOWN";
    }
}

void StateMachine::printCurrentState() const {
    LOG_INFO("Current State: %s, Federation Status: %s, Time in State: %lums", 
             getStateString(), getStatusString(), getTimeInCurrentState());
}

void StateMachine::printTransitionHistory() const {
    LOG_INFO("Previous State: %s, Time Since Transition: %lums, Error Count: %d", 
             getStateString(), getTimeSinceLastTransition(), errorCount);
}

// Static policy methods
bool StateMachine::federationHasPriority(DeviceState currentState) {
    return (currentState == FEDERATION_TRAINING || currentState == FEDERATION_RECOVERY);
}

bool StateMachine::requiresModelUnload(DeviceState state) {
    return (state == FEDERATION_TRAINING);
}

bool StateMachine::allowsSensorOperation(DeviceState state) {
    // Sensors can run in all states except error
    return (state != DEVICE_ERROR);
}