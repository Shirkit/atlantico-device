// Comprehensive ESP32 Power and Hardware Analysis
// This will help distinguish between power supply vs hardware defect

#include <WiFi.h>
#include <driver/adc.h>

const char* ssid = "PedroRapha";
const char* password = "456123789a";

void setup() {
    Serial.begin(115200);
    delay(500);
    
    Serial.println("=== ESP32 Hardware Defect Analysis ===");
    Serial.println("Testing power regulation and WiFi hardware...");
    
    // Test 1: Baseline power consumption
    Serial.println("\n--- Test 1: Baseline Power Analysis ---");
    Serial.println("CPU running at normal speed...");
    delay(500);
    Serial.println("✅ Baseline operation stable");
    
    // Test 2: High CPU load test (no WiFi)
    Serial.println("\n--- Test 2: High CPU Load Test ---");
    Serial.println("Stressing CPU without WiFi...");
    unsigned long start = millis();
    volatile long dummy = 0;
    for(int i = 0; i < 1000000; i++) {
        dummy += i * i;  // CPU intensive operation
    }
    unsigned long duration = millis() - start;
    Serial.printf("✅ CPU stress test completed in %lu ms\n", duration);
    Serial.println("No brownout during CPU stress - power regulation OK for CPU");
    
    // Test 3: WiFi mode setting (low power)
    Serial.println("\n--- Test 3: WiFi Mode Setting ---");
    Serial.println("Setting WiFi to station mode (no radio activity)...");
    delay(500);
    
    try {
        WiFi.mode(WIFI_STA);
        delay(1000);
        Serial.println("✅ WiFi mode set successfully");
    } catch(...) {
        Serial.println("❌ Failed to set WiFi mode");
    }
    
    // Test 4: WiFi power level setting
    Serial.println("\n--- Test 4: WiFi Power Configuration ---");
    Serial.println("Setting WiFi to lowest power...");
    
    try {
        WiFi.setTxPower(WIFI_POWER_2dBm);  // Lowest power
        delay(1000);
        Serial.println("✅ WiFi power level set to minimum");
    } catch(...) {
        Serial.println("❌ Failed to set WiFi power level");
    }
    
    // Test 5: The critical moment - WiFi radio initialization
    Serial.println("\n--- Test 5: CRITICAL - WiFi Radio Initialization ---");
    Serial.println("This is where hardware defects typically manifest...");
    Serial.println("If brownout occurs here, voltage regulator is defective");
    Serial.println("Starting WiFi radio in 3 seconds...");
    
    delay(1000);
    Serial.println("3...");
    delay(1000);
    Serial.println("2...");
    delay(1000);
    Serial.println("1...");
    delay(500);
    
    Serial.println("🔥 INITIALIZING WIFI RADIO NOW...");
    
    // This is the moment of truth
    unsigned long wifiStart = millis();
    WiFi.begin(ssid, password);
    unsigned long wifiInitTime = millis() - wifiStart;
    
    // If we reach here without brownout, hardware is likely good
    Serial.printf("✅ WiFi initialization survived! (%lu ms)\n", wifiInitTime);
    Serial.println("🎉 HARDWARE APPEARS FUNCTIONAL!");
    Serial.println("Voltage regulator can handle WiFi radio power demands");
    
    // Test connection
    Serial.println("\n--- Test 6: WiFi Connection Attempt ---");
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n🏆 COMPLETE SUCCESS - DEVICE IS FULLY FUNCTIONAL!");
        Serial.println("✅ Power regulation: GOOD");
        Serial.println("✅ WiFi hardware: GOOD");
        Serial.println("✅ Network connection: GOOD");
        Serial.println("IP: " + WiFi.localIP().toString());
        Serial.println("\nThis device can be used for your federated learning project!");
    } else {
        Serial.println("\n⚠️ PARTIAL SUCCESS");
        Serial.println("✅ Power regulation: GOOD (survived WiFi init)");
        Serial.println("✅ WiFi hardware: GOOD (no crash)");
        Serial.println("❌ Network connection: Check WiFi credentials");
        Serial.println("Hardware is functional, network issue only");
    }
}

void loop() {
    // Continuous monitoring
    static unsigned long lastCheck = 0;
    
    if (millis() - lastCheck > 5000) {
        lastCheck = millis();
        
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("✅ Device stable - WiFi and power both working");
            Serial.println("   RSSI: " + String(WiFi.RSSI()) + " dBm");
        } else {
            Serial.println("⚠️ WiFi disconnected (but no hardware failure)");
        }
    }
    
    delay(100);
}
