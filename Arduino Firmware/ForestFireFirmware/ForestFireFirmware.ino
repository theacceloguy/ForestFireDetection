#include <WiFi.h>
#include <Wire.h>
#include <Firebase_ESP_Client.h>
#include <DFRobot_ENS160.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"
#include <time.h>

// ====== USER CONFIGURATION ======
String sensorId = "TestBenchSensor01";  // Change for each device
#define WIFI_SSID "Aikriti"
#define WIFI_PASSWORD "Aikriti@510"
#define API_KEY "AIzaSyDO1PYg0pSfvg54cY_q4dCwLA4VvdCaEYg"
#define DATABASE_URL "https://forest-fire-detection-97ee1-default-rtdb.asia-southeast1.firebasedatabase.app/"
#define On_Board_LED 2

// ====== Firebase + Time Setup ======
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
bool signupOK = false;

// ====== ENS160 Sensor Setup ======
DFRobot_ENS160_I2C ens160(&Wire, 0x53);  // Default I2C address for ENS160

// ====== Timing ======
unsigned long sendDataPrevMillis = 0;
const long sendInterval = 5000; // 10 seconds

// ====== Random GPS Location (Mumbai area) ======
float latitude = 19.0790;
float longitude = 72.9080;

void setup() {
  Serial.begin(115200);
  pinMode(On_Board_LED, OUTPUT);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print(".");
  }
  Serial.println("\n WiFi Connected");

  // Time sync for timestamps
  configTime(19800, 0, "pool.ntp.org", "time.nist.gov");  // IST (UTC +5:30)

  // Firebase config
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  config.token_status_callback = tokenStatusCallback;

  if (Firebase.signUp(&config, &auth, "", "")) {
    Serial.println("Firebase signup successful");
    signupOK = true;
  } else {
    Serial.printf("Firebase signup failed: %s\n", config.signer.signupError.message.c_str());
  }

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  // ENS160 Init
  if (ens160.begin() != 0) {
    Serial.println("ENS160 not found. Check wiring.");
    while (1);
  }
  Serial.println("ENS160 Sensor initialized");

  ens160.setTempAndHum(25.0, 50.0); // Set default ambient conditions
}

void loop() {
  if (Firebase.ready() && signupOK &&
      (millis() - sendDataPrevMillis > sendInterval || sendDataPrevMillis == 0)) {

    sendDataPrevMillis = millis();

    // Read ENS160 values
    uint16_t eco2 = ens160.getECO2();
    uint16_t tvoc = ens160.getTVOC();
    uint8_t aqi = ens160.getAQI();

    // Fire detection logic based on VOC
    bool fireDetected = false;
    if ((tvoc > 800) || (eco2 > 1500) || (aqi > 3)) {
      fireDetected = true;
    }

    // Get timestamp
    time_t nowTime = time(nullptr);
    if (nowTime < 10000) {
      struct tm timeinfo;
      if (getLocalTime(&timeinfo)) nowTime = mktime(&timeinfo);
    }
    String timestamp = String(nowTime);

    // Debug output
    Serial.println("---- ENS160 Readings ----");
    Serial.printf("TVOC: %d ppb\n", tvoc);
    Serial.printf("eCO2: %d ppm\n", eco2);
    Serial.printf("AQI: %d\n", aqi);
    Serial.printf("🔥Fire Detected: %s\n", fireDetected ? "YES" : "NO");
    Serial.println("-------------------------");

    // Firebase Upload - Latest
    String path = "/Sensors/" + sensorId + "/latest";
    Firebase.RTDB.setInt(&fbdo, path + "/TVOC_ppb", tvoc);
    Firebase.RTDB.setInt(&fbdo, path + "/eCO2_ppm", eco2);
    Firebase.RTDB.setInt(&fbdo, path + "/AQI", aqi);
    Firebase.RTDB.setBool(&fbdo, path + "/FireDetected", fireDetected);
    Firebase.RTDB.setFloat(&fbdo, path + "/Latitude", latitude);
    Firebase.RTDB.setFloat(&fbdo, path + "/Longitude", longitude);
    Firebase.RTDB.setInt(&fbdo, path + "/timestamp", nowTime);

    // Firebase Upload - History
    String historyPath = "/Sensors/" + sensorId + "/history/" + timestamp;
    Firebase.RTDB.setInt(&fbdo, historyPath + "/TVOC_ppb", tvoc);
    Firebase.RTDB.setInt(&fbdo, historyPath + "/eCO2_ppm", eco2);
    Firebase.RTDB.setInt(&fbdo, historyPath + "/AQI", aqi);
    Firebase.RTDB.setBool(&fbdo, historyPath + "/FireDetected", fireDetected);
    Firebase.RTDB.setInt(&fbdo, historyPath + "/timestamp", nowTime);

    // Firebase - Alerts
    if (fireDetected) {
      Firebase.RTDB.pushString(&fbdo, "/Sensors/" + sensorId + "/alerts", "🔥 Fire detected at " + timestamp);
      digitalWrite(On_Board_LED, HIGH);
    } else {
      digitalWrite(On_Board_LED, LOW);
    }

    Serial.printf("Data uploaded at %s\n", timestamp.c_str());
  }
}
