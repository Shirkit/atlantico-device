#ifndef STATE_MACHINE_H_
#define STATE_MACHINE_H_

#include "Types.h"
#include "Config.h"

#if USE_ADVANCED_LOGGER
#include "Logger.h"
#endif

/**
 * @brief Centralized state machine for coordinating device behavior
 * 
 * This state machine manages the overall device state and federation status,
 * providing clean separation of concerns and validated state transitions.
 * 
 * Device States control resource allocation:
 * - DEVICE_INITIALIZING: Boot sequence, loading config
 * - INFERENCE_MODE: Running local inference 
 * - FEDERATION_TRAINING: Active federation training (highest priority)
 * - FEDERATION_RECOVERY: Recovering from federation errors
 * - DEVICE_ERROR: Critical error requiring intervention
 * 
 * Federation Status provides metadata about federation participation:
 * - FEDERATE_NONE: Not participating
 * - FEDERATE_SUBSCRIBED: Waiting for training round
 * - FEDERATE_TRAINING: Actively training
 * - FEDERATE_ENDING: Completing training round
 */
class StateMachine {
private:
    DeviceState currentState;
    FederateStatus federateStatus;
    DeviceState previousState;
    
    // Transition validation
    bool isValidTransition(DeviceState from, DeviceState to) const;
    bool isValidStatusTransition(FederateStatus from, FederateStatus to) const;
    
    // State timing
    unsigned long stateEntryTime;
    unsigned long lastTransitionTime;
    
    // Error tracking
    int errorCount;
    DeviceState stateBeforeError;
    static const int MAX_ERROR_COUNT = 3;
    
public:
    StateMachine();
    
    // State management
    bool transitionTo(DeviceState newState);
    bool setFederateStatus(FederateStatus newStatus);
    
    // State queries
    DeviceState getCurrentState() const { return currentState; }
    FederateStatus getFederateStatus() const { return federateStatus; }
    DeviceState getPreviousState() const { return previousState; }
    
    // State checks
    bool isInferenceAllowed() const;
    bool isFederationActive() const;
    bool isInitialized() const;
    bool isInErrorState() const;
    bool canStartFederation() const;
    bool canStopFederation() const;
    
    // Timing
    unsigned long getTimeInCurrentState() const;
    unsigned long getTimeSinceLastTransition() const;
    
    // Error handling
    void reportError();
    void clearErrors();
    bool shouldRetryAfterError() const;
    
    // Debugging
    const char* getStateString() const;
    const char* getStatusString() const;
    void printCurrentState() const;
    void printTransitionHistory() const;
    
    // State machine policies
    static bool federationHasPriority(DeviceState currentState);
    static bool requiresModelUnload(DeviceState state);
    static bool allowsSensorOperation(DeviceState state);
};

#endif /* STATE_MACHINE_H_ */