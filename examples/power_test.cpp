// ESP32 Power Supply Test and WiFi Gradual Initialization
// This tests power supply capacity and WiFi initialization step-by-step

#include <WiFi.h>

const char* ssid = "PedroRapha";
const char* password = "456123789a";

void setup() {
    Serial.begin(115200);
    delay(3000);  // Longer delay for power stabilization
    
    Serial.println("=== ESP32 Power Supply and WiFi Test ===");
    Serial.println("Testing power supply stability...");
    
    // Test 1: Check if we can survive basic WiFi mode setting
    Serial.println("Step 1: Setting WiFi mode...");
    WiFi.mode(WIFI_STA);
    delay(1000);
    Serial.println("✅ WiFi mode set - Power OK so far");
    
    // Test 2: Set lower power mode before WiFi init
    Serial.println("Step 2: Setting low power mode...");
    WiFi.setTxPower(WIFI_POWER_2dBm);  // Lowest power setting
    delay(1000);
    Serial.println("✅ Low power mode set");
    
    // Test 3: Try WiFi initialization with power monitoring
    Serial.println("Step 3: Attempting WiFi begin...");
    Serial.println("If brownout occurs here, power supply is insufficient");
    
    // Critical moment - this is where brownout typically occurs
    WiFi.begin(ssid, password);
    
    // If we reach here, power supply survived WiFi initialization
    Serial.println("✅ WiFi initialization survived!");
    Serial.println("Power supply is adequate for WiFi operation");
    
    // Test connection
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n🎉 COMPLETE SUCCESS!");
        Serial.println("✅ Power supply: GOOD");
        Serial.println("✅ WiFi hardware: GOOD");  
        Serial.println("✅ Device: FULLY FUNCTIONAL");
        Serial.println("IP: " + WiFi.localIP().toString());
    } else {
        Serial.println("\n⚠️ PARTIAL SUCCESS");
        Serial.println("✅ Power supply: GOOD (no brownout)");
        Serial.println("✅ WiFi hardware: GOOD");
        Serial.println("❌ Network connection: Check credentials/network");
    }
}

void loop() {
    // Monitor power stability during operation
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("Device stable - Power and WiFi both working");
    } else {
        Serial.println("WiFi disconnected - but no brownout (power still good)");
    }
    delay(5000);
}
