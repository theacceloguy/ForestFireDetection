String sensorId = "TestBenchSensor01"; // Unique sensor ID (change for each device)


#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME680.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"
#include <time.h>  
#define WIFI_SSID "Aikriti"
#define WIFI_PASSWORD "Aikriti@510"

#define API_KEY "AIzaSyDO1PYg0pSfvg54cY_q4dCwLA4VvdCaEYg"
#define DATABASE_URL "https://forest-fire-detection-97ee1-default-rtdb.asia-southeast1.firebasedatabase.app/"

#define FIRE_THRESHOLD 50.0
#define On_Board_LED 2

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

unsigned long sendDataPrevMillis = 0;
const long sendInterval = 10000;  // 10 seconds
bool signupOK = false;

Adafruit_BME680 bme;



// Sample coordinates (randomized for testing)
float latitude = 19.07 + (float)(random(-100, 100)) / 1000.0;  // Mumbai area
float longitude = 72.87 + (float)(random(-100, 100)) / 1000.0;

void setup() {
  Serial.begin(115200);
  pinMode(On_Board_LED, OUTPUT);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("\nConnected.");

  // Time sync
  configTime(19800, 0, "pool.ntp.org", "time.nist.gov");  // IST (UTC+5:30)

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

  // Init BME680
  if (!bme.begin()) {
    Serial.println("Could not find a valid BME680 sensor");
    while (1);
  }

  bme.setTemperatureOversampling(BME680_OS_8X);
  bme.setHumidityOversampling(BME680_OS_2X);
  bme.setPressureOversampling(BME680_OS_4X);
  bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
  bme.setGasHeater(320, 150);  // 320°C for 150 ms
}

void loop() {
  if (Firebase.ready() && signupOK && (millis() - sendDataPrevMillis > sendInterval || sendDataPrevMillis == 0)) {
    sendDataPrevMillis = millis();

    unsigned long start = millis();
    while (!bme.performReading()) {
      delay(100);
      if (millis() - start > 2000) return;
    }

    float temperature = bme.temperature;
    float pressure = bme.pressure / 100.0;
    float humidity = bme.humidity;
    float gasResistance = bme.gas_resistance / 1000.0;  // in kOhms
    float altitude = bme.readAltitude(1013.25);
    bool fireDetected = (temperature > FIRE_THRESHOLD);

    // Get timestamp via NTP
    time_t nowTime = time(nullptr);
    if (nowTime < 10000) {
      struct tm timeinfo;
      if (getLocalTime(&timeinfo)) nowTime = mktime(&timeinfo);
    }
    String timestamp = String(nowTime);

    String path = "/Sensors/" + sensorId + "/latest";

    Firebase.RTDB.setFloat(&fbdo, path + "/Temperature", temperature);
    Firebase.RTDB.setFloat(&fbdo, path + "/Humidity", humidity);
    Firebase.RTDB.setFloat(&fbdo, path + "/Pressure_hPa", pressure);
    Firebase.RTDB.setFloat(&fbdo, path + "/GasResistance_KOhm", gasResistance);
    Firebase.RTDB.setFloat(&fbdo, path + "/Altitude_m", altitude);
    Firebase.RTDB.setFloat(&fbdo, path + "/Latitude", latitude);
    Firebase.RTDB.setFloat(&fbdo, path + "/Longitude", longitude);
    Firebase.RTDB.setBool(&fbdo, path + "/FireDetected", fireDetected);
    Firebase.RTDB.setInt(&fbdo, path + "/timestamp", nowTime);

    // Save historical data
    String historyPath = "/Sensors/" + sensorId + "/history/" + timestamp;
    Firebase.RTDB.setFloat(&fbdo, historyPath + "/Temperature", temperature);
    Firebase.RTDB.setInt(&fbdo, historyPath + "/timestamp", nowTime);

    // Generate alert if fire detected
    if (fireDetected) {
      Firebase.RTDB.pushString(&fbdo, "/Sensors/" + sensorId + "/alerts", "Fire detected at " + timestamp);
    }

    Serial.printf("Uploaded at %s: Temp=%.2f°C\n", timestamp.c_str(), temperature);
  }
}
