#include "Types.h"
#include "Config.h"

// classClassifierMetricts implementations
DFLOAT classClassifierMetricts::totalPredictions() {
    return truePositives + trueNegatives + falsePositives + falseNegatives;
}

DFLOAT classClassifierMetricts::accuracy() {
    DFLOAT total = totalPredictions();
    if (total == 0) return 0.0;
    return (truePositives + trueNegatives) / total;
}

DFLOAT classClassifierMetricts::precision() {
    if (truePositives + falsePositives == 0) return 0.0;
    return truePositives / (DFLOAT)(truePositives + falsePositives);
}

DFLOAT classClassifierMetricts::recall() {
    if (truePositives + falseNegatives == 0) return 0.0;
    return truePositives / (DFLOAT)(truePositives + falseNegatives);
}

DFLOAT classClassifierMetricts::f1Score() {
    DFLOAT p = precision();
    DFLOAT r = recall();
    if (p + r == 0) return 0.0;
    return 2 * (p * r) / (p + r);
}

// multiClassClassifierMetrics implementations
DFLOAT multiClassClassifierMetrics::totalPredictions() {
    DFLOAT total = 0;
    for (unsigned int i = 0; i < numberOfClasses; i++) {
        total += metrics[i].totalPredictions();
    }
    return total;
}

DFLOAT multiClassClassifierMetrics::accuracy() {
    DFLOAT totalCorrect = 0;
    DFLOAT total = 0;
    for (unsigned int i = 0; i < numberOfClasses; i++) {
        totalCorrect += metrics[i].truePositives + metrics[i].trueNegatives;
        total += metrics[i].totalPredictions();
    }
    if (total == 0) return 0.0;
    return totalCorrect / total;
}

DFLOAT multiClassClassifierMetrics::precision() {
    DFLOAT totalPrecision = 0;
    for (unsigned int i = 0; i < numberOfClasses; i++) {
        totalPrecision += metrics[i].precision();
    }
    return totalPrecision / numberOfClasses;
}

DFLOAT multiClassClassifierMetrics::recall() {
    DFLOAT totalRecall = 0;
    for (unsigned int i = 0; i < numberOfClasses; i++) {
        totalRecall += metrics[i].recall();
    }
    return totalRecall / numberOfClasses;
}

DFLOAT multiClassClassifierMetrics::f1Score() {
    DFLOAT totalF1 = 0;
    for (unsigned int i = 0; i < numberOfClasses; i++) {
        totalF1 += metrics[i].f1Score();
    }
    return totalF1 / numberOfClasses;
}

DFLOAT multiClassClassifierMetrics::balancedAccuracy() {
    return accuracy(); // Simplified implementation
}

DFLOAT multiClassClassifierMetrics::balancedPrecision() {
    return precision(); // Simplified implementation
}

DFLOAT multiClassClassifierMetrics::balancedRecall() {
    return recall(); // Simplified implementation
}

DFLOAT multiClassClassifierMetrics::balancedF1Score() {
    return f1Score(); // Simplified implementation
}

void multiClassClassifierMetrics::print() {
    LOG_INFO("=== Multi-Class Classifier Metrics ===");
    LOG_INFO("Number of classes: %d", numberOfClasses);
    LOG_INFO("Total accuracy: %.4f", accuracy());
    LOG_INFO("Average precision: %.4f", precision());
    LOG_INFO("Average recall: %.4f", recall());
    LOG_INFO("Average F1 score: %.4f", f1Score());
    LOG_INFO("Training time: %lu ms", trainingTime);
    LOG_INFO("Epochs: %lu", epochs);
    
    for (unsigned int i = 0; i < numberOfClasses; i++) {
        LOG_DEBUG("Class %d - TP: %d, TN: %d, FP: %d, FN: %d", 
                 i, metrics[i].truePositives, metrics[i].trueNegatives, 
                 metrics[i].falsePositives, metrics[i].falseNegatives);
    }
}

// DeviceConfig implementations
void DeviceConfig::reset() {
    currentRound = -1;
    currentFederateState = FederateState_NONE;
    newModelState = ModelState_IDLE;
    
    if (currentModelMetrics) {
        delete currentModelMetrics;
        currentModelMetrics = nullptr;
    }
    
    if (loadedFederateModelConfig) {
        delete loadedFederateModelConfig;
        loadedFederateModelConfig = nullptr;
    }
}

DeviceConfig::~DeviceConfig() {
    reset();
}
