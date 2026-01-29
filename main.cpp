#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Wire.h>
#include <BH1750.h>
#include <RTClib.h>
#include <PZEM004Tv30.h>
#include <SD.h>
#include <SPI.h>
#include <EEPROM.h>
#include <LiquidCrystal_I2C.h>
#include <time.h>

// WiFi credentials
const char* ssid = "Kamaluddin";
const char* password = "kamal12345678";

// Supabase configuration
const char* supabaseUrl = "https://uyjqtvbcscincmsypmqo.supabase.co";
const char* supabaseKey = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6InV5anF0dmJjc2NpbmNtc3lwbXFvIiwicm9sZSI6ImFub24iLCJpYXQiOjE3NTQ0NzQ2MzYsImV4cCI6MjA3MDA1MDYzNn0.aCQSGTbcYfQU4cDV-I8nkr7AXAEplWvvZKEyB85LAW8";
     
#define DHT_PIN 27
#define DHT_TYPE DHT22
#define SOIL_MOISTURE_PIN 34       
#define TEMP_SENSOR_PIN 35          
#define FLOW_SENSOR_PIN 15         
#define PUMP_PIN 33               
#define FAN_PIN 2                
#define WATER_VALVE_PIN 13        
#define VITAMIN_VALVE_PIN 32       

 
#define WATER_TANK_TRIG_PIN 26     
#define WATER_TANK_ECHO_PIN 25
#define VITAMIN_TANK_TRIG_PIN 14   
#define VITAMIN_TANK_ECHO_PIN 12

// I2C Pins
#define SDA_PIN 21                 // I2C Data
#define SCL_PIN 22                 // I2C Clock

#define NTP_SERVER "pool.ntp.org"
#define GMT_OFFSET_SEC 25200
#define DAYLIGHT_OFFSET_SEC 0

// SD Card Module Pins (Micro SD Card Reader Writer Module)
#define SD_CS_PIN 5                // CS (Chip Select) pin
#define SD_MOSI_PIN 23             // MOSI (Master Out Slave In) - DI pin on module
#define SD_MISO_PIN 19             // MISO (Master In Slave Out) - DO pin on module  
#define SD_SCK_PIN 18              // SCK (Serial Clock) - CLK pin on module

// LED Status Pins
#define WIFI_LED_PIN 4            // WiFi status LED
#define SYSTEM_LED_PIN 0          // System status LED

// LCD I2C Address (biasanya 0x27 atau 0x3F)
#define LCD_COLUMNS 20
#define LCD_ROWS 4

// Tank specifications
#define WATER_TANK_HEIGHT 50
#define VITAMIN_TANK_HEIGHT 30
#define MIN_DISTANCE_CM 2
#define MAX_DISTANCE_CM 400

// Safety defines
#define MAX_IRRIGATION_TIME 600000
#define SENSOR_READ_TIMEOUT 5000
#define WIFI_TIMEOUT_MS 20000
#define I2C_TIMEOUT_MS 1000
#define WIFI_RECONNECT_INTERVAL 30000  // 5 minutes
#define MAX_OFFLINE_LOGS 1000           // Maximum logs to store offline

// EEPROM addresses for persistent storage
#define EEPROM_SIZE 512
#define EEPROM_ADDR_THRESHOLDS 0
#define EEPROM_ADDR_SCHEDULE 100
#define EEPROM_ADDR_SYSTEM_STATE 200

// Sensor objects
DHT dht(DHT_PIN, DHT_TYPE);
OneWire oneWire(TEMP_SENSOR_PIN);
DallasTemperature temperatureSensors(&oneWire);
BH1750 lightMeter;
RTC_DS3231 rtc;

// Flow sensor variables
volatile int flowPulseCount = 0;
float flowRate = 0.0;
unsigned long flowLastTime = 0;
float totalFlowVolume = 0.0;

// Timing variables
unsigned long lastSensorRead = 0;
unsigned long lastDataSend = 0;
unsigned long lastAutoControl = 0;
unsigned long lastThresholdSync = 0;
unsigned long lastScheduleCheck = 0;
unsigned long lastWiFiCheck = 0;
unsigned long lastOfflineSync = 0;
const unsigned long SENSOR_INTERVAL = 5000;
const unsigned long SEND_INTERVAL = 30000;
const unsigned long CONTROL_INTERVAL = 60000;
const unsigned long THRESHOLD_SYNC_INTERVAL = 30000;
const unsigned long SCHEDULE_CHECK_INTERVAL = 15000;
const unsigned long OFFLINE_SYNC_INTERVAL = 600000;  // 10 minutes

// Tambahkan di bagian global variables (sekitar baris 100-150)
String lastIrrigationTrigger = "NONE"; // Track what triggered last irrigation
unsigned long lastIrrigationTime = 0;

// Tambahkan di bagian sensor objects
LiquidCrystal_I2C lcd(0x27, LCD_COLUMNS, LCD_ROWS); // Akan diinisialisasi ulang jika alamat berbeda

// Tambahkan variabel untuk LCD control
unsigned long lastLcdUpdate = 0;
const unsigned long LCD_UPDATE_INTERVAL = 2000; // Update setiap 2 detik
int currentLcdPage = 0;
const int TOTAL_LCD_PAGES = 4;
bool lcdAvailable = false;

// System status flags
bool rtcAvailable = false;
bool lightSensorAvailable = false;
bool wifiConnected = false;
bool sdCardAvailable = false;
bool offlineMode = false;
int offlineLogCount = 0;

bool ntpTimeSet = false;
unsigned long lastNTPSync = 0;
const unsigned long NTP_SYNC_INTERVAL = 3600000;

// LED status variables
unsigned long lastLedUpdate = 0;
bool ledState = false;

// System thresholds
struct SystemThresholds {
  float maxTemperature = 35.0;
  float minSoilMoisture = 42.0;
  float lowWaterLevel = 20.0;
  float lowVitaminLevel = 15.0;
  float minLightLevel = 300.0;
} thresholds;

// Irrigation schedule
struct IrrigationSchedule {
  bool enabled = true;
  int morningHour = 8;
  int morningMinute = 0;
  int eveningHour = 18;
  int eveningMinute = 0;
  bool weekendMode = true;
  bool tankRotation = true;
  bool useWaterTank = true;
} schedule;

// System status
struct SystemStatus {
  bool pumpActive = false;
  bool fanActive = false;
  bool autoMode = true;
  bool waterValveActive = false;
  bool vitaminValveActive = false;
  float lastSoilMoisture = 0;
  float lastTemperature = 0;
  float lastWaterLevel = 0;
  float lastVitaminLevel = 0;
  unsigned long pumpStartTime = 0;
  bool morningIrrigationDone = false;
  bool eveningIrrigationDone = false;
  float lastVoltage = 0;
  float lastCurrent = 0;
  float lastPower = 0;
  float lastEnergy = 0;
  float lastFrequency = 0;
  float lastPowerFactor = 0;
} systemStatus;

// Forward declarations (tambahkan setelah variabel global)
String getCurrentTimeString();
bool uploadOfflineFile(String filename);
void syncThresholds();
void syncSchedule();
void initializeOfflineSupport();
void setupUltrasonicSensors();
void testUltrasonicSensors();
void readSensorsImproved();
void sendSensorData();
void fetchPZEMDataFromDatabase();
void performAutoControl();
void checkWiFiStatus();
void checkScheduledIrrigation();
void executeScheduledIrrigationV2(String reason, bool useVitamin);
void startIrrigationV2(String reason, bool useVitamin);
void checkDeviceControl();
void checkPowerQuality();
void stopIrrigation(String reason);
void startFan(String reason);
void stopFan(String reason);
void logIrrigationEvent(String action, String reason, String tankType);

bool uploadCSVRowToDatabase(String csvRow);
void logIrrigationEventCSV(String action, String reason, String tankType, unsigned long irrigDuration);
void logSystemEventCSV(String eventType, String message);
void displayOfflineModeNotification();
void displayOnlineModeNotification();
void displaySyncProgress(int syncedFiles, int totalFiles);

// Flow sensor interrupt handler
void IRAM_ATTR flowPulseCounter() {
  flowPulseCount++;
}

// LED control functions
void updateStatusLEDs() {
  unsigned long currentTime = millis();
  
  // WiFi LED status
  if (wifiConnected) {
    digitalWrite(WIFI_LED_PIN, HIGH);  // Solid ON when connected
  } else {
    // Blink slowly when disconnected
    if (currentTime - lastLedUpdate >= 1000) {
      ledState = !ledState;
      digitalWrite(WIFI_LED_PIN, ledState);
      lastLedUpdate = currentTime;
    }
  }
  
  // System LED status
  if (systemStatus.pumpActive || systemStatus.fanActive) {
    // Fast blink when system is active
    if (currentTime - lastLedUpdate >= 250) {
      digitalWrite(SYSTEM_LED_PIN, !digitalRead(SYSTEM_LED_PIN));
    }
  } else {
    // Slow pulse when idle
    if (currentTime - lastLedUpdate >= 2000) {
      digitalWrite(SYSTEM_LED_PIN, !digitalRead(SYSTEM_LED_PIN));
    }
  }
}

// EEPROM functions for persistent storage
void saveThresholdsToEEPROM() {
  EEPROM.put(EEPROM_ADDR_THRESHOLDS, thresholds);
  EEPROM.commit();
  Serial.println("💾 Thresholds saved to EEPROM");
}

void loadThresholdsFromEEPROM() {
  SystemThresholds loadedThresholds;
  EEPROM.get(EEPROM_ADDR_THRESHOLDS, loadedThresholds);
  
  // Validate loaded data
  if (loadedThresholds.maxTemperature > 0 && loadedThresholds.maxTemperature < 60) {
    thresholds = loadedThresholds;
    Serial.println("📖 Thresholds loaded from EEPROM");
  } else {
    Serial.println("⚠️ Invalid EEPROM data, using defaults");
    saveThresholdsToEEPROM();
  }
}

void saveScheduleToEEPROM() {
  EEPROM.put(EEPROM_ADDR_SCHEDULE, schedule);
  EEPROM.commit();
  Serial.println("💾 Schedule saved to EEPROM");
}

void loadScheduleFromEEPROM() {
  IrrigationSchedule loadedSchedule;
  EEPROM.get(EEPROM_ADDR_SCHEDULE, loadedSchedule);
  
  // Validate loaded data
  if (loadedSchedule.morningHour >= 0 && loadedSchedule.morningHour <= 23) {
    schedule = loadedSchedule;
    Serial.println("📖 Schedule loaded from EEPROM");
  } else {
    Serial.println("⚠️ Invalid EEPROM schedule, using defaults");
    saveScheduleToEEPROM();
  }
}

String getSensorDataCSVHeader() {
  return "timestamp,temperature,humidity,soil_moisture,water_level,vitamin_level,"
         "water_temp,vitamin_temp,flow_rate,total_volume,voltage,current,power,"
         "energy,frequency,power_factor,light_level";
}

String getIrrigationLogCSVHeader() {
  return "timestamp,action,reason,tank_type,duration_sec,soil_before,soil_after,"
         "water_level,vitamin_level,trigger_type";
}

String getSystemLogCSVHeader() {
  return "timestamp,event_type,message,system_status";
}

bool ensureCSVFile(String filename, String header) {
  if (!sdCardAvailable) return false;
  
  if (!filename.startsWith("/")) {
    filename = "/" + filename;
  }
  
  Serial.printf("📝 Checking file: %s\n", filename.c_str());
  
  // Extract directory
  int lastSlash = filename.lastIndexOf('/');
  if (lastSlash > 0) {
    String dir = filename.substring(0, lastSlash);
    
    if (!SD.exists(dir)) {
      Serial.printf("📁 Creating directory: %s\n", dir.c_str());
      if (!SD.mkdir(dir)) {
        Serial.printf("❌ Failed to create directory: %s\n", dir.c_str());
        return false;
      }
      delay(200);
      Serial.printf("✅ Directory created: %s\n", dir.c_str());
    }
  }
  
  // Check if file exists
  if (SD.exists(filename)) {
    Serial.printf("✓ File exists: %s\n", filename.c_str());
    return true;
  }
  
  // Create new file
  Serial.printf("📄 Creating new file: %s\n", filename.c_str());
  
  File file = SD.open(filename, FILE_WRITE);
  if (!file) {
    Serial.printf("❌ Failed to open file for writing: %s\n", filename.c_str());
    return false;
  }
  
  // Write header
  size_t written = file.println(header);
  file.flush(); // PENTING: Paksa write ke SD card
  delay(100);   // Beri waktu SD card menulis
  file.close();
  
  delay(100); // Delay setelah close
  
  if (written > 0) {
    Serial.printf("✅ File created with header (%d bytes): %s\n", written, filename.c_str());
    
    // Verify file was created
    if (SD.exists(filename)) {
      Serial.printf("✓ File verified: %s\n", filename.c_str());
      return true;
    } else {
      Serial.printf("⚠️ File not found after creation: %s\n", filename.c_str());
      return false;
    }
  } else {
    Serial.printf("❌ Failed to write header: %s\n", filename.c_str());
    return false;
  }
}

// SD Card functions with SPI interface
bool initializeSDCard() {
  Serial.println("💽 Initializing Micro SD Card Module...");
  
  // End previous session
  SD.end();
  delay(500);
  
  // Initialize SPI
  SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  delay(100);
  
  Serial.printf("📌 SD Card Pins: CS=%d, MOSI=%d, MISO=%d, SCK=%d\n", 
                SD_CS_PIN, SD_MOSI_PIN, SD_MISO_PIN, SD_SCK_PIN);
  
  // Multiple initialization attempts
  int attempts = 0;
  bool initialized = false;
  
  while (attempts < 5 && !initialized) {
    Serial.printf("Attempt %d/5... ", attempts + 1);
    
    if (SD.begin(SD_CS_PIN)) {
      initialized = true;
      Serial.println("✅ SUCCESS");
    } else {
      Serial.println("❌ FAILED");
      attempts++;
      delay(1000);
    }
  }
  
  if (!initialized) {
    Serial.println("❌ SD Card initialization failed!");
    Serial.println("🔧 Check: wiring, card insertion, FAT32 format, power supply");
    return false;
  }
  
  // Verify card
  uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("❌ No SD card detected!");
    return false;
  }
  
  // Display card info
  Serial.println("✅ SD Card initialized!");
  Serial.printf("📋 Type: %s\n", 
                cardType == CARD_MMC ? "MMC" : 
                cardType == CARD_SD ? "SD" : 
                cardType == CARD_SDHC ? "SDHC" : "UNKNOWN");
  
  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  uint64_t totalBytes = SD.totalBytes();
  uint64_t usedBytes = SD.usedBytes();
  
  Serial.printf("📏 Size: %llu MB\n", cardSize);
  Serial.printf("💾 Used: %.2f MB / %.2f MB (%.1f%%)\n", 
                usedBytes / (1024.0 * 1024.0),
                totalBytes / (1024.0 * 1024.0),
                (usedBytes * 100.0) / totalBytes);
  
  // Create directory structure
  Serial.println("📁 Creating directories...");
  
  String dirs[] = {"/sensor_data", "/irrigation_logs", "/system_logs", "/config"};
  
  for (int i = 0; i < 4; i++) {
    if (!SD.exists(dirs[i])) {
      if (SD.mkdir(dirs[i])) {
        Serial.printf("   ✅ Created: %s\n", dirs[i].c_str());
        delay(100);
      } else {
        Serial.printf("   ❌ Failed: %s\n", dirs[i].c_str());
      }
    } else {
      Serial.printf("   ✓ Exists: %s\n", dirs[i].c_str());
    }
  }
  
  // Test write
  Serial.println("🧪 Testing CSV write...");
  String testFile = "/test_write.csv";
  File file = SD.open(testFile, FILE_WRITE);
  
  if (file) {
    file.println("timestamp,test_value");
    file.println(getCurrentTimeString() + ",123.45");
    file.close();
    
    // Verify
    if (SD.exists(testFile)) {
      file = SD.open(testFile, FILE_READ);
      if (file) {
        String header = file.readStringUntil('\n');
        String data = file.readStringUntil('\n');
        file.close();
        Serial.println("✅ Write test PASSED");
        Serial.println("   Header: " + header);
        Serial.println("   Data: " + data);
      }
      SD.remove(testFile);
    }
  } else {
    Serial.println("❌ Write test FAILED");
    return false;
  }
  
  Serial.println("🎯 SD Card ready for CSV logging!");
  return true;
}

void logToSDCard(String filename, String data) {
  if (!sdCardAvailable) {
    Serial.println("⚠️ SD Card not available, skipping log");
    return;
  }
  
  // Pastikan filename dimulai dengan /
  if (!filename.startsWith("/")) {
    filename = "/" + filename;
  }
  
  // Extract directory path
  int lastSlash = filename.lastIndexOf('/');
  if (lastSlash > 0) {
    String dir = filename.substring(0, lastSlash);
    
    // Check and create directory if needed
    if (!SD.exists(dir)) {
      Serial.printf("📁 Creating directory: %s\n", dir.c_str());
      if (!SD.mkdir(dir)) {
        Serial.printf("❌ Failed to create directory: %s\n", dir.c_str());
        return;
      }
      delay(100);
    }
  }
  
  // Try to open file with retry mechanism
  File file;
  int attempts = 0;
  bool fileOpened = false;
  
  while (attempts < 3 && !fileOpened) {
    file = SD.open(filename, FILE_APPEND);
    if (file) {
      fileOpened = true;
    } else {
      attempts++;
      delay(100);
    }
  }
  
  if (!fileOpened) {
    Serial.printf("❌ Failed to open: %s\n", filename.c_str());
    
    // Auto retry SD card initialization
    static unsigned long lastSDRetry = 0;
    if (millis() - lastSDRetry > 30000) {
      Serial.println("🔄 Reinitializing SD card...");
      SD.end();
      delay(500);
      sdCardAvailable = initializeSDCard();
      lastSDRetry = millis();
    }
    return;
  }
  
  // Write CSV data
  size_t bytesWritten = file.println(data);
  file.flush();
  file.close();
  
  if (bytesWritten > 0) {
    Serial.printf("✅ CSV logged: %s (%d bytes)\n", filename.c_str(), bytesWritten);
  } else {
    Serial.printf("❌ Write failed: %s\n", filename.c_str());
  }
}

// ===== CSV DATA FORMATTING FUNCTIONS =====

// Format sensor data sebagai CSV row
String formatSensorDataCSV() {
  String csvRow = "";
  
  // Timestamp
  csvRow += getCurrentTimeString() + ",";
  
  // Temperature & Humidity
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();
  csvRow += (isnan(temperature) ? "" : String(temperature, 2)) + ",";
  csvRow += (isnan(humidity) ? "" : String(humidity, 2)) + ",";
  
  // Soil moisture
  csvRow += String(systemStatus.lastSoilMoisture, 2) + ",";
  
  // Tank levels
  csvRow += (systemStatus.lastWaterLevel >= 0 ? String(systemStatus.lastWaterLevel, 2) : "") + ",";
  csvRow += (systemStatus.lastVitaminLevel >= 0 ? String(systemStatus.lastVitaminLevel, 2) : "") + ",";
  
  // Temperature sensors
  temperatureSensors.requestTemperatures();
  float waterTemp = temperatureSensors.getTempCByIndex(0);
  float vitaminTemp = temperatureSensors.getTempCByIndex(1);
  csvRow += (waterTemp != DEVICE_DISCONNECTED_C ? String(waterTemp, 2) : "") + ",";
  csvRow += (vitaminTemp != DEVICE_DISCONNECTED_C ? String(vitaminTemp, 2) : "") + ",";
  
  // Flow data
  csvRow += String(flowRate, 2) + ",";
  csvRow += String(totalFlowVolume, 2) + ",";
  
  // Power data
  csvRow += (systemStatus.lastVoltage > 0 ? String(systemStatus.lastVoltage, 2) : "") + ",";
  csvRow += (systemStatus.lastCurrent >= 0 ? String(systemStatus.lastCurrent, 3) : "") + ",";
  csvRow += (systemStatus.lastPower >= 0 ? String(systemStatus.lastPower, 2) : "") + ",";
  csvRow += (systemStatus.lastEnergy >= 0 ? String(systemStatus.lastEnergy, 3) : "") + ",";
  csvRow += (systemStatus.lastFrequency > 0 ? String(systemStatus.lastFrequency, 1) : "") + ",";
  csvRow += (systemStatus.lastPowerFactor >= 0 ? String(systemStatus.lastPowerFactor, 3) : "") + ",";
  
  // Light level
  if (lightSensorAvailable) {
    float lightLevel = lightMeter.readLightLevel();
    csvRow += (lightLevel >= 0 ? String(lightLevel, 2) : "");
  }
  
  return csvRow;
}

// Format irrigation log sebagai CSV row
String formatIrrigationLogCSV(String action, String reason, String tankType, 
                               unsigned long irrigDuration, float soilBefore, float soilAfter) {
  String csvRow = "";
  
  csvRow += getCurrentTimeString() + ",";
  csvRow += action + ",";
  csvRow += "\"" + reason + "\"" + ",";  // Quote reason karena mungkin ada koma
  csvRow += tankType + ",";
  csvRow += String(irrigDuration) + ",";
  csvRow += String(soilBefore, 2) + ",";
  csvRow += String(soilAfter, 2) + ",";
  csvRow += String(systemStatus.lastWaterLevel, 2) + ",";
  csvRow += String(systemStatus.lastVitaminLevel, 2) + ",";
  
  // Determine trigger type
  String triggerType = "UNKNOWN";
  if (reason.indexOf("Morning scheduled") >= 0) triggerType = "SCHEDULED_MORNING";
  else if (reason.indexOf("Evening scheduled") >= 0) triggerType = "SCHEDULED_EVENING";
  else if (reason.indexOf("moisture") >= 0) triggerType = "AUTO_MOISTURE";
  else if (reason.indexOf("Manual") >= 0) triggerType = "MANUAL";
  
  csvRow += triggerType;
  
  return csvRow;
}

// Format system log sebagai CSV row
String formatSystemLogCSV(String eventType, String message) {
  String csvRow = "";
  
  csvRow += getCurrentTimeString() + ",";
  csvRow += eventType + ",";
  csvRow += "\"" + message + "\"" + ",";  // Quote message
  
  // System status summary
  String status = "PUMP:" + String(systemStatus.pumpActive ? "ON" : "OFF") + 
                  "|FAN:" + String(systemStatus.fanActive ? "ON" : "OFF") +
                  "|AUTO:" + String(systemStatus.autoMode ? "ON" : "OFF");
  csvRow += status;
  
  return csvRow;
}

void createOfflineLogEntry() {
  if (!sdCardAvailable) return;
  
  DateTime now = rtc.now();
  
  // PERBAIKAN: Format dengan leading zero
  char dateBuffer[12];
  sprintf(dateBuffer, "%04d-%02d-%02d", now.year(), now.month(), now.day());
  String filename = "/sensor_data/" + String(dateBuffer) + ".csv";
  
  ensureCSVFile(filename, getSensorDataCSVHeader());
  
  String csvData = formatSensorDataCSV();
  logToSDCard(filename, csvData);
  
  offlineLogCount++;
}

void syncOfflineCSVToDatabase() {
  if (!wifiConnected || !sdCardAvailable) return;
  
  Serial.println("🔄 === Syncing Offline CSV Data ===");
  
  File root = SD.open("/sensor_data");
  if (!root) {
    Serial.println("❌ Cannot open sensor_data directory");
    return;
  }
  
  int syncedFiles = 0;
  int totalFiles = 0;
  int totalRows = 0;
  
  File file = root.openNextFile();
  while (file) {
    String filename = file.name();
    
    if (!file.isDirectory() && filename.endsWith(".csv")) {
      totalFiles++;
      Serial.printf("\n📄 Processing: %s\n", filename.c_str());
      
      file.close();
      
      String fullPath = "/sensor_data/" + filename;
      File csvFile = SD.open(fullPath, FILE_READ);
      
      if (csvFile) {
        String header = csvFile.readStringUntil('\n');
        
        int rowCount = 0;
        int uploadedRows = 0;
        
        while (csvFile.available()) {
          String line = csvFile.readStringUntil('\n');
          line.trim();
          
          if (line.length() < 10) continue;
          
          rowCount++;
          
          if (uploadCSVRowToDatabase(line)) {
            uploadedRows++;
          }
          
          if (rowCount % 10 == 0) {
            delay(500);
            Serial.printf("   Progress: %d rows uploaded\n", uploadedRows);
          }
        }
        
        csvFile.close();
        
        Serial.printf("   ✅ Uploaded %d/%d rows from %s\n", 
                      uploadedRows, rowCount, filename.c_str());
        
        if (uploadedRows > 0) {
          syncedFiles++;
          totalRows += uploadedRows;
        }
      }
      
      file = root.openNextFile();
    } else {
      file = root.openNextFile();
    }
  }
  
  root.close();
  
  Serial.println("\n📊 Sync Summary:");
  Serial.printf("   Files processed: %d\n", totalFiles);
  Serial.printf("   Files synced: %d\n", syncedFiles);
  Serial.printf("   Total rows uploaded: %d\n", totalRows);
  
  if (syncedFiles > 0) {
    logSystemEventCSV("SYNC_COMPLETE", 
                     "Synced " + String(syncedFiles) + " files, " + 
                     String(totalRows) + " rows");
  }
  
  offlineLogCount = 0;
}

bool uploadOfflineFile(String filename) {
  File file = SD.open(filename);
  if (!file) return false;
  
  HTTPClient http;
  http.setTimeout(15000);
  http.begin(String(supabaseUrl) + "/rest/v1/sensor_readings");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Prefer", "return=minimal");
  http.addHeader("apikey", supabaseKey);
  http.addHeader("Authorization", String("Bearer ") + supabaseKey);
  
  String line;
  int successCount = 0;
  int totalLines = 0;
  
  while (file.available()) {
    line = file.readStringUntil('\n');
    line.trim();
    
    if (line.length() > 10) {  // Valid JSON line
      totalLines++;
      int httpResponseCode = http.POST(line);
      
      if (httpResponseCode == 201) {
        successCount++;
      }
      
      delay(100);  // Small delay between requests
    }
  }
  
  file.close();
  http.end();
  
  Serial.printf("📤 Uploaded %d/%d entries from %s\n", successCount, totalLines, filename.c_str());
  return (successCount > 0);
}

void logIrrigationEvent(String action, String reason, String tankType) {
  if (!wifiConnected || offlineMode) return;
  
  HTTPClient http;
  http.setTimeout(10000);
  http.begin(String(supabaseUrl) + "/rest/v1/irrigation_logs");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Prefer", "return=minimal");
  http.addHeader("apikey", supabaseKey);
  http.addHeader("Authorization", String("Bearer ") + supabaseKey);
  
  JsonDocument doc;
  doc["action"] = action;
  doc["reason"] = reason;
  doc["tank_type"] = tankType;
  doc["soil_moisture"] = systemStatus.lastSoilMoisture;
  doc["water_level"] = systemStatus.lastWaterLevel;
  doc["vitamin_level"] = systemStatus.lastVitaminLevel;
  
  String jsonString;
  serializeJson(doc, jsonString);
  
  int httpResponseCode = http.POST(jsonString);
  
  if (httpResponseCode == 201) {
    Serial.println("✅ Irrigation event logged to database");
  } else {
    Serial.printf("❌ Failed to log irrigation event: %d\n", httpResponseCode);
  }
  
  http.end();
}

void enterOfflineMode() {
  if (offlineMode) return;
  
  offlineMode = true;
  Serial.println("🔌 === ENTERING OFFLINE MODE ===");
  Serial.println("📱 Using last known settings from EEPROM");
  Serial.println("💾 Logging data to SD Card (CSV format)");
  
  loadThresholdsFromEEPROM();
  loadScheduleFromEEPROM();
  
  if (sdCardAvailable) {
    logSystemEventCSV("OFFLINE_MODE", "Entered offline mode - WiFi disconnected");
  }
  
  if (lcdAvailable) {
    displayOfflineModeNotification();
  }
}

void exitOfflineMode() {
  if (!offlineMode) return;
  
  offlineMode = false;
  Serial.println("🌐 === EXITING OFFLINE MODE ===");
  Serial.println("📡 Reconnected to network");
  
  if (sdCardAvailable) {
    logSystemEventCSV("ONLINE_MODE", "Exited offline mode - WiFi reconnected");
    
    // Sync akan dilakukan otomatis di loop
    Serial.println("📤 Offline CSV files will be synced in next cycle");
  }
  
  syncThresholds();
  syncSchedule();
  
  if (lcdAvailable) {
    displayOnlineModeNotification();
  }
}

String getCurrentTimeString() {
  if (rtcAvailable) {
    DateTime now = rtc.now();
    char buffer[20];
    sprintf(buffer, "%04d-%02d-%02d %02d:%02d:%02d", 
            now.year(), now.month(), now.day(),
            now.hour(), now.minute(), now.second());
    return String(buffer);
  } else if (ntpTimeSet) {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      char buffer[20];
      sprintf(buffer, "%04d-%02d-%02d %02d:%02d:%02d",
              timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
              timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
      return String(buffer);
    }
  }
  return "TIME_UNKNOWN";
}

bool initializeNTP() {
  if (!wifiConnected) {
    Serial.println("❌ Cannot initialize NTP: WiFi not connected");
    return false;
  }
  
  Serial.println("🌐 Initializing NTP time sync...");
  Serial.printf("   NTP Server: %s\n", NTP_SERVER);
  Serial.printf("   Timezone: GMT+7 (WIB)\n");
  
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
  
  struct tm timeinfo;
  int attempts = 0;
  
  while (!getLocalTime(&timeinfo) && attempts < 10) {
    Serial.print(".");
    delay(1000);
    attempts++;
  }
  
  if (attempts >= 10) {
    Serial.println("\n❌ Failed to obtain time from NTP server");
    return false;
  }
  
  Serial.println("\n✅ NTP time synchronized successfully!");
  char buffer[80];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S %Z", &timeinfo);
  Serial.printf("📅 Current time: %s\n", buffer);
  
  ntpTimeSet = true;
  lastNTPSync = millis();
  
  // Jika RTC tersedia, update RTC dengan waktu NTP
  if (rtcAvailable) {
    rtc.adjust(DateTime(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, 
                       timeinfo.tm_mday, timeinfo.tm_hour, 
                       timeinfo.tm_min, timeinfo.tm_sec));
    Serial.println("✅ RTC updated with NTP time");
  }
  
  return true;
}

void syncNTPTime() {
  if (!wifiConnected || offlineMode) return;
  
  unsigned long currentTime = millis();
  if (currentTime - lastNTPSync < NTP_SYNC_INTERVAL) return;
  
  Serial.println("🔄 Periodic NTP sync...");
  if (initializeNTP()) {
    lastNTPSync = currentTime;
  }
}

bool getSystemTime(int &year, int &month, int &day, 
                   int &hour, int &minute, int &second) {
  if (rtcAvailable) {
    DateTime now = rtc.now();
    year = now.year();
    month = now.month();
    day = now.day();
    hour = now.hour();
    minute = now.minute();
    second = now.second();
    return true;
  } else if (ntpTimeSet) {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      year = timeinfo.tm_year + 1900;
      month = timeinfo.tm_mon + 1;
      day = timeinfo.tm_mday;
      hour = timeinfo.tm_hour;
      minute = timeinfo.tm_min;
      second = timeinfo.tm_sec;
      return true;
    }
  }
  return false;
}

String getTimeSource() {
  if (rtcAvailable) {
    return "RTC DS3231";
  } else if (ntpTimeSet) {
    return "NTP Server";
  }
  return "NONE";
}

// I2C Scanner function
void scanI2CDevices() {
  Serial.println("🔍 Scanning I2C devices...");
  
  byte error, address;
  int deviceCount = 0;
  
  for (address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();
    
    if (error == 0) {
      Serial.printf("✅ I2C device found at 0x%02X", address);
      
      switch (address) {
        case 0x68:
          Serial.println(" (DS3231 RTC)");
          break;
        case 0x23:
        case 0x5C:
          Serial.println(" (BH1750 Light Sensor)");
          break;
        default:
          Serial.println(" (Unknown)");
          break;
      }
      deviceCount++;
    }
  }
  
  if (deviceCount == 0) {
    Serial.println("❌ No I2C devices found!");
  } else {
    Serial.printf("✅ Found %d I2C device(s)\n", deviceCount);
  }
}

// Enhanced RTC initialization
bool initializeRTC() {
  Serial.println("🕒 Initializing RTC DS3231...");
  
  for (int attempt = 1; attempt <= 3; attempt++) {
    Serial.printf("Attempt %d/3...", attempt);
    
    if (rtc.begin()) {
      Serial.println(" SUCCESS!");
      
      DateTime now = rtc.now();
      Serial.printf("RTC Current Time: %04d-%02d-%02d %02d:%02d:%02d\n",
                    now.year(), now.month(), now.day(),
                    now.hour(), now.minute(), now.second());
      
      // Check if RTC lost power or has invalid time
      if (rtc.lostPower() || now.year() < 2020 || now.year() > 2050) {
        Serial.println("⚠️ RTC time invalid or lost power");
        
        // Try to set from compile time first
        Serial.println("Setting RTC to compile time...");
        rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
        
        delay(100);
        now = rtc.now();
        
        // If still invalid, set to a reasonable default
        if (now.year() < 2020 || now.year() > 2050) {
          Serial.println("Setting RTC to default time (2025-01-01 00:00:00)");
          rtc.adjust(DateTime(2025, 1, 1, 0, 0, 0));
        }
        
        delay(100);
        now = rtc.now();
      }
      
      // Mark as available even if time needs adjustment
      rtcAvailable = true;
      
      Serial.printf("✅ RTC Time Set: %04d-%02d-%02d %02d:%02d:%02d\n",
                    now.year(), now.month(), now.day(),
                    now.hour(), now.minute(), now.second());
      Serial.printf("✅ RTC Temperature: %.2f°C\n", rtc.getTemperature());
      
      return true;
    } else {
      Serial.println(" FAILED!");
    }
    
    delay(1000);
  }
  
  Serial.println("❌ RTC initialization failed completely!");
  return false;
}

// FUNGSI PERBAIKAN UNTUK SENSOR SRF05/HC-SR04
float measureDistanceImproved(int trigPin, int echoPin) {
  // Pastikan pin dalam keadaan LOW
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  
  // Kirim pulse 10us HIGH untuk trigger
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  
  // Baca echo dengan timeout 30ms (untuk jarak maksimal ~5m)
  unsigned long pulseDuration = pulseIn(echoPin, HIGH, 30000);
  
  // Jika timeout (duration = 0), return error
  if (pulseDuration == 0) {
    Serial.printf("Sensor timeout on pins TRIG:%d ECHO:%d\n", trigPin, echoPin);
    return -1.0;
  }

  float distance = (pulseDuration * 0.0343) / 2.0;

  // Validasi range sensor (2cm - 400cm untuk HC-SR04/SRF05)
  if (distance < 2.0 || distance > 400.0) {
    Serial.printf("Distance out of range: %.2f cm on pins TRIG:%d ECHO:%d\n", 
                  distance, trigPin, echoPin);
    return -1.0;
  }
  
  return distance;
}

// Function to convert distance to tank level percentage
float distanceToLevel(float distance, float tankHeight) {
  if (distance < 0) return -1;
  
  float waterHeight = tankHeight - distance;
  if (waterHeight < 0) waterHeight = 0;
  if (waterHeight > tankHeight) waterHeight = tankHeight;
  
  return (waterHeight / tankHeight) * 100.0;
}

bool uploadCSVRowToDatabase(String csvRow) {
  if (!wifiConnected || offlineMode) return false;
  
  // Parse CSV row dan convert ke JSON
  int commaPos[20];
  int commaCount = 0;
  
  for (int i = 0; i < csvRow.length() && commaCount < 20; i++) {
    if (csvRow[i] == ',') {
      commaPos[commaCount++] = i;
    }
  }
  
  if (commaCount < 10) return false; // Invalid CSV format
  
  HTTPClient http;
  http.setTimeout(10000);
  http.begin(String(supabaseUrl) + "/rest/v1/sensor_readings");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Prefer", "return=minimal");
  http.addHeader("apikey", supabaseKey);
  http.addHeader("Authorization", String("Bearer ") + supabaseKey);
  
  // Build JSON from CSV
  JsonDocument doc;
  
  int startPos = 0;
  
  // timestamp
  String timestamp = csvRow.substring(startPos, commaPos[0]);
  doc["created_at"] = timestamp;
  
  // temperature
  startPos = commaPos[0] + 1;
  String temp = csvRow.substring(startPos, commaPos[1]);
  if (temp.length() > 0) doc["temperature"] = temp.toFloat();
  
  // humidity
  startPos = commaPos[1] + 1;
  String hum = csvRow.substring(startPos, commaPos[2]);
  if (hum.length() > 0) doc["humidity"] = hum.toFloat();
  
  // soil_moisture
  startPos = commaPos[2] + 1;
  String soil = csvRow.substring(startPos, commaPos[3]);
  if (soil.length() > 0) doc["soil_moisture"] = soil.toFloat();
  
  // water_level
  startPos = commaPos[3] + 1;
  String water = csvRow.substring(startPos, commaPos[4]);
  if (water.length() > 0) doc["water_tank_level"] = water.toFloat();
  
  // vitamin_level
  startPos = commaPos[4] + 1;
  String vitamin = csvRow.substring(startPos, commaPos[5]);
  if (vitamin.length() > 0) doc["vitamin_tank_level"] = vitamin.toFloat();
  
  String jsonString;
  serializeJson(doc, jsonString);
  
  int httpResponseCode = http.POST(jsonString);
  http.end();
  
  return (httpResponseCode == 201);
}

void logIrrigationEventCSV(String action, String reason, String tankType, unsigned long irrigDuration) {
  if (!sdCardAvailable) return;
  
  DateTime now = rtc.now();
  
  // PERBAIKAN: Format dengan leading zero
  char dateBuffer[12];
  sprintf(dateBuffer, "%04d-%02d-%02d", now.year(), now.month(), now.day());
  String filename = "/irrigation_logs/" + String(dateBuffer) + ".csv";
  
  ensureCSVFile(filename, getIrrigationLogCSVHeader());
  
  float soilBefore = systemStatus.lastSoilMoisture;
  float soilAfter = systemStatus.lastSoilMoisture;
  
  String csvData = formatIrrigationLogCSV(action, reason, tankType, 
                                          irrigDuration, soilBefore, soilAfter);
  logToSDCard(filename, csvData);
}

void logSystemEventCSV(String eventType, String message) {
  if (!sdCardAvailable) return;
  
  DateTime now = rtc.now();
  
  // PERBAIKAN: Format dengan leading zero
  char monthBuffer[8];
  sprintf(monthBuffer, "%04d-%02d", now.year(), now.month());
  String filename = "/system_logs/" + String(monthBuffer) + "_system.csv";
  
  ensureCSVFile(filename, getSystemLogCSVHeader());
  
  String csvData = formatSystemLogCSV(eventType, message);
  logToSDCard(filename, csvData);
}

void diagnosticSDCard() {
  Serial.println("\n🔍 === SD CARD DIAGNOSTIC (CSV MODE) ===");
  
  if (!sdCardAvailable) {
    Serial.println("❌ SD Card unavailable - reinitializing...");
    sdCardAvailable = initializeSDCard();
    if (!sdCardAvailable) {
      Serial.println("❌ Reinitialization failed");
      return;
    }
  }
  
  Serial.println("✅ SD Card available");
  
  Serial.println("\n📁 Directory Check:");
  String dirs[] = {"/sensor_data", "/irrigation_logs", "/system_logs", "/config"};
  for (int i = 0; i < 4; i++) {
    bool exists = SD.exists(dirs[i]);
    Serial.printf("   %s: %s\n", dirs[i].c_str(), exists ? "✅" : "❌");
  }
  
  Serial.println("\n📄 CSV Files:");
  for (int i = 0; i < 3; i++) {
    File dir = SD.open(dirs[i]);
    if (dir) {
      File entry = dir.openNextFile();
      while (entry) {
        if (!entry.isDirectory() && String(entry.name()).endsWith(".csv")) {
          Serial.printf("   %s/%s (%d bytes)\n", 
                        dirs[i].c_str(), entry.name(), entry.size());
        }
        entry = dir.openNextFile();
      }
      dir.close();
    }
  }
  
  Serial.println("\n🧪 CSV Write Test:");
  DateTime now = rtc.now();
  String testDate = String(now.year()) + "-" + String(now.month()) + "-" + String(now.day());
  
  String testFile = "/sensor_data/" + testDate + "_test.csv";
  File f = SD.open(testFile, FILE_WRITE);
  if (f) {
    f.println("timestamp,value");
    f.println(getCurrentTimeString() + ",999");
    f.close();
    Serial.println("   ✅ Write PASSED");
    
    f = SD.open(testFile, FILE_READ);
    if (f) {
      Serial.println("   Content:");
      while (f.available()) {
        Serial.println("      " + f.readStringUntil('\n'));
      }
      f.close();
    }
    SD.remove(testFile);
  } else {
    Serial.println("   ❌ Write FAILED");
  }
  
  Serial.println("═══════════════════════════════════════\n");
}

void viewCSVFile(String filename) {
  if (!sdCardAvailable) {
    Serial.println("❌ SD Card not available");
    return;
  }
  
  if (!SD.exists(filename)) {
    Serial.println("❌ File not found: " + filename);
    return;
  }
  
  File file = SD.open(filename, FILE_READ);
  if (!file) {
    Serial.println("❌ Cannot open file: " + filename);
    return;
  }
  
  Serial.println("\n📊 === CSV File Contents ===");
  Serial.println("File: " + filename);
  Serial.println("Size: " + String(file.size()) + " bytes");
  Serial.println("────────────────────────────────────────");
  
  int lineCount = 0;
  while (file.available() && lineCount < 20) {
    String line = file.readStringUntil('\n');
    Serial.println(line);
    lineCount++;
  }
  
  if (file.available()) {
    Serial.println("... (more lines available)");
  }
  
  file.close();
  Serial.println("────────────────────────────────────────");
  Serial.printf("Total lines shown: %d\n", lineCount);
  Serial.println("════════════════════════════════════════\n");
}

// WiFi connection with timeout
bool connectToWiFi() {
  Serial.println("🌐 Connecting to WiFi...");
  WiFi.begin(ssid, password);
  
  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - startTime) < WIFI_TIMEOUT_MS) {
    delay(500);
    Serial.print(".");
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.print("✅ WiFi Connected! IP: ");
    Serial.println(WiFi.localIP());
    return true;
  } else {
    Serial.println();
    Serial.println("❌ WiFi connection failed!");
    return false;
  }
}

// ========== IMPLEMENTASI FUNGSI LCD ==========

bool initializeLCD() {
  Serial.println("Initializing LCD 20x4...");
  
  // Coba beberapa alamat I2C yang umum untuk LCD
  uint8_t lcdAddresses[] = {0x27, 0x3F, 0x26, 0x20};
  bool lcdFound = false;
  
  for (int i = 0; i < 4; i++) {
    Wire.beginTransmission(lcdAddresses[i]);
    if (Wire.endTransmission() == 0) {
      Serial.printf("LCD found at address 0x%02X\n", lcdAddresses[i]);
      // Reinitialize LCD dengan alamat yang benar
      lcd = LiquidCrystal_I2C(lcdAddresses[i], LCD_COLUMNS, LCD_ROWS);
      lcdFound = true;
      break;
    }
  }
  
  if (!lcdFound) {
    Serial.println("LCD not found on I2C bus!");
    return false;
  }
  
  lcd.init();
  lcd.backlight();
  
  // Test LCD
  lcd.setCursor(0, 0);
  lcd.print("ESP32 Irrigation");
  lcd.setCursor(0, 1);
  lcd.print("System v2.1");
  lcd.setCursor(0, 2);
  lcd.print("Initializing...");
  lcd.setCursor(0, 3);
  lcd.print("Please wait...");
  
  delay(2000);
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("LCD Test OK");
  delay(1000);
  
  Serial.println("LCD 20x4 initialized successfully");
  return true;
}

void displayStartupScreen() {
  if (!lcdAvailable) return;
  
  lcd.clear();
  lcd.backlight();
  
  lcd.setCursor(2, 0);
  lcd.print("ESP32 IRRIGATION");
  
  lcd.setCursor(6, 1);
  lcd.print("SYSTEM v2.1");
  
  lcd.setCursor(4, 2);
  lcd.print("STARTING UP");
  
  for (int i = 0; i < 8; i++) {
    lcd.setCursor(8 + i, 3);
    lcd.print(".");
    delay(300);
  }
  
  delay(1000);
  lcd.clear();
}

void displayMainPage() {
  if (!lcdAvailable) return;
  
  lcd.clear();
  
  // Baris 1: Status koneksi dan waktu
  lcd.setCursor(0, 0);
  if (wifiConnected && !offlineMode) {
    lcd.print("ONLINE ");
  } else {
    lcd.print("OFFLINE");
    if (millis() % 1000 < 500) {
      lcd.setCursor(7, 0);
      lcd.print("*");
    }
  }
  
  // Waktu dengan fallback
  int year, month, day, hour, minute, second;
  if (getSystemTime(year, month, day, hour, minute, second)) {
    lcd.setCursor(9, 0);
    lcd.printf("%02d:%02d:%02d", hour, minute, second);
  } else {
    lcd.setCursor(9, 0);
    lcd.print("--:--:--");
  }
  
  // Temperature & Humidity
  lcd.setCursor(0, 1);
  if (systemStatus.lastTemperature > -50 && systemStatus.lastTemperature < 60) {
    lcd.printf("T:%.1fC", systemStatus.lastTemperature);
  } else {
    lcd.print("T:ERR ");
  }
  
  float humidity = dht.readHumidity();
  lcd.setCursor(10, 1);
  if (!isnan(humidity) && humidity >= 0 && humidity <= 100) {
    lcd.printf("H:%.0f%%", humidity);
  } else {
    lcd.print("H:ERR");
  }
  
  // Soil Moisture
  lcd.setCursor(0, 2);
  if (systemStatus.lastSoilMoisture >= 0 && systemStatus.lastSoilMoisture <= 100) {
    lcd.printf("Soil:%.0f%%", systemStatus.lastSoilMoisture);
  } else {
    lcd.print("Soil:ERR");
  }
  
  // Status
  lcd.setCursor(13, 2);
  if (systemStatus.pumpActive) {
    lcd.print("PUMP");
  } else if (systemStatus.fanActive) {
    lcd.print("FAN ");
  } else {
    lcd.print("IDLE");
  }
  
  // Tank levels
  lcd.setCursor(0, 3);
  if (systemStatus.lastWaterLevel >= 0 && systemStatus.lastWaterLevel <= 100) {
    lcd.printf("W:%.0f%%", systemStatus.lastWaterLevel);
  } else {
    lcd.print("W:ERR");
  }
  
  lcd.setCursor(8, 3);
  if (systemStatus.lastVitaminLevel >= 0 && systemStatus.lastVitaminLevel <= 100) {
    lcd.printf("V:%.0f%%", systemStatus.lastVitaminLevel);
  } else {
    lcd.print("V:ERR");
  }
  
  lcd.setCursor(16, 3);
  if (systemStatus.autoMode) {
    lcd.print("AUTO");
  } else {
    lcd.print("MAN ");
  }
}

void displayPowerPage() {
  if (!lcdAvailable) return;
  
  lcd.clear();
  lcd.setCursor(2, 0);
  lcd.print("== POWER MONITOR ==");
  
  // Voltage & Frequency
  lcd.setCursor(0, 1);
  if (!isnan(systemStatus.lastVoltage) && systemStatus.lastVoltage > 0) {
    lcd.printf("V:%.0fV", systemStatus.lastVoltage);
  } else {
    lcd.print("V:ERR ");
  }
  
  lcd.setCursor(8, 1);
  if (!isnan(systemStatus.lastFrequency) && systemStatus.lastFrequency > 0) {
    lcd.printf("F:%.0fHz", systemStatus.lastFrequency);
  } else {
    lcd.print("F:ERR");
  }
  
  // Current & Power
  lcd.setCursor(0, 2);
  if (!isnan(systemStatus.lastCurrent) && systemStatus.lastCurrent >= 0) {
    lcd.printf("I:%.2fA", systemStatus.lastCurrent);
  } else {
    lcd.print("I:ERR   ");
  }
  
  lcd.setCursor(10, 2);
  if (!isnan(systemStatus.lastPower) && systemStatus.lastPower >= 0) {
    lcd.printf("P:%.0fW", systemStatus.lastPower);
  } else {
    lcd.print("P:ERR");
  }
  
  // Energy & Power Factor
  lcd.setCursor(0, 3);
  if (!isnan(systemStatus.lastEnergy) && systemStatus.lastEnergy >= 0) {
    lcd.printf("E:%.2fkWh", systemStatus.lastEnergy);
  } else {
    lcd.print("E:ERR    ");
  }
  
  lcd.setCursor(12, 3);
  if (!isnan(systemStatus.lastPowerFactor) && systemStatus.lastPowerFactor >= 0) {
    lcd.printf("PF:%.2f", systemStatus.lastPowerFactor);
  } else {
    lcd.print("PF:ERR");
  }
}

void displayFlowPage() {
  if (!lcdAvailable) return;
  
  lcd.clear();
  lcd.setCursor(3, 0);
  lcd.print("== FLOW & LIGHT ==");
  
  lcd.setCursor(0, 1);
  if (flowRate >= 0 && flowRate < 999) {
    lcd.printf("Flow: %.1f L/min", flowRate);
  } else {
    lcd.print("Flow: ERROR     ");
  }
  
  lcd.setCursor(0, 2);
  if (totalFlowVolume >= 0 && totalFlowVolume < 9999) {
    lcd.printf("Total: %.1f L", totalFlowVolume);
  } else {
    lcd.print("Total: ERROR    ");
  }
  
  lcd.setCursor(0, 3);
  if (lightSensorAvailable) {
    float lightLevel = lightMeter.readLightLevel();
    if (lightLevel >= 0 && lightLevel < 99999) {
      lcd.printf("Light: %.0f lux", lightLevel);
    } else {
      lcd.print("Light: ERROR    ");
    }
  } else {
    lcd.print("Light: N/A      ");
  }
}

void displaySystemPage() {
  if (!lcdAvailable) return;
  
  lcd.clear();
  lcd.setCursor(1, 0);
  if (offlineMode) {
    lcd.print("== OFFLINE MODE ==");
  } else {
    lcd.print("== SYSTEM STATUS ==");
  }
  
  lcd.setCursor(0, 1);
  lcd.print("RTC:");
  lcd.print(rtcAvailable ? "OK" : "ER");
  
  lcd.setCursor(7, 1);
  lcd.print("SD:");
  lcd.print(sdCardAvailable ? "OK" : "ER");
  
  lcd.setCursor(13, 1);
  lcd.print("NTP:");
  lcd.print(ntpTimeSet ? "OK" : "ER");
  
  lcd.setCursor(0, 2);
  String timeSource = getTimeSource();
  lcd.printf("Time: %-12s", timeSource.c_str());
  
  lcd.setCursor(0, 3);
  if (offlineMode && offlineLogCount < 9999) {
    lcd.printf("Logs: %d entries", offlineLogCount);
  } else {
    unsigned long uptimeMin = millis() / 60000;
    if (uptimeMin < 9999) {
      lcd.printf("Uptime: %lu min", uptimeMin);
    } else {
      lcd.print("Uptime: 9999+ min");
    }
  }
}

void displayAlert(String alertType, String message) {
  if (!lcdAvailable) return;
  
  lcd.clear();
  lcd.setCursor(3, 0);
  lcd.print("!!! ALERT !!!");
  
  lcd.setCursor(0, 1);
  if (alertType == "TANK_LOW") {
    lcd.print("TANK LOW:");
  } else if (alertType == "TEMP_HIGH") {
    lcd.print("TEMP HIGH:");
  } else if (alertType == "POWER_ISSUE") {
    lcd.print("POWER ISSUE:");
  } else {
    lcd.print("SYSTEM ALERT:");
  }
  
  if (message.length() <= 20) {
    lcd.setCursor(0, 2);
    lcd.print(message);
  } else {
    lcd.setCursor(0, 2);
    lcd.print(message.substring(0, 20));
    lcd.setCursor(0, 3);
    lcd.print(message.substring(20, min(40, (int)message.length())));
  }
  
  // Blink backlight
  for (int i = 0; i < 3; i++) {
    lcd.noBacklight();
    delay(250);
    lcd.backlight();
    delay(250);
  }
}

void showLCDAlert(String alertType, String message) {
  displayAlert(alertType, message);
}

void updateLCD() {
  if (!lcdAvailable) return;
  
  unsigned long currentTime = millis();
  
  if (currentTime - lastLcdUpdate >= LCD_UPDATE_INTERVAL) {
    
    // Cek alert
    static unsigned long lastAlertCheck = 0;
    if (currentTime - lastAlertCheck > 5000) {
      
      if (systemStatus.lastWaterLevel >= 0 && systemStatus.lastWaterLevel < thresholds.lowWaterLevel) {
        displayAlert("TANK_LOW", "Water tank critical");
        lastLcdUpdate = currentTime;
        lastAlertCheck = currentTime;
        return;
      }
      
      if (systemStatus.lastVitaminLevel >= 0 && systemStatus.lastVitaminLevel < thresholds.lowVitaminLevel) {
        displayAlert("TANK_LOW", "Vitamin tank critical");
        lastLcdUpdate = currentTime;
        lastAlertCheck = currentTime;
        return;
      }
      
      if (systemStatus.lastTemperature > thresholds.maxTemperature + 5) {
        displayAlert("TEMP_HIGH", "Temperature critical");
        lastLcdUpdate = currentTime;
        lastAlertCheck = currentTime;
        return;
      }
      
      if (systemStatus.lastVoltage > 0 && (systemStatus.lastVoltage > 260 || systemStatus.lastVoltage < 180)) {
        displayAlert("POWER_ISSUE", "Voltage out of range");
        lastLcdUpdate = currentTime;
        lastAlertCheck = currentTime;
        return;
      }
      
      lastAlertCheck = currentTime;
    }
    
    // Halaman normal
    switch (currentLcdPage) {
      case 0:
        displayMainPage();
        break;
      case 1:
        displayPowerPage();
        break;
      case 2:
        displayFlowPage();
        break;
      case 3:
        displaySystemPage();
        break;
      default:
        currentLcdPage = 0;
        displayMainPage();
        break;
    }
    
    currentLcdPage = (currentLcdPage + 1) % TOTAL_LCD_PAGES;
    lastLcdUpdate = currentTime;
  }
}

void displayOfflineModeNotification() {
  if (!lcdAvailable) return;
  
  lcd.clear();
  lcd.setCursor(1, 0);
  lcd.print("ENTERING OFFLINE");
  lcd.setCursor(6, 1);
  lcd.print("MODE");
  lcd.setCursor(0, 2);
  lcd.print("WiFi disconnected");
  lcd.setCursor(1, 3);
  lcd.print("Using saved config");
  
  for (int i = 0; i < 3; i++) {
    lcd.noBacklight();
    delay(300);
    lcd.backlight();
    delay(300);
  }
  
  delay(2000);
}

void displayOnlineModeNotification() {
  if (!lcdAvailable) return;
  
  lcd.clear();
  lcd.setCursor(3, 0);
  lcd.print("BACK ONLINE!");
  lcd.setCursor(1, 1);
  lcd.print("WiFi reconnected");
  lcd.setCursor(0, 2);
  lcd.print("Syncing data...");
  lcd.setCursor(4, 3);
  lcd.print("Please wait...");
  
  delay(3000);
}

void displaySyncProgress(int syncedFiles, int totalFiles) {
  if (!lcdAvailable || totalFiles == 0) return;
  
  lcd.clear();
  lcd.setCursor(2, 0);
  lcd.print("SYNCING DATA...");
  
  lcd.setCursor(0, 1);
  lcd.printf("Files: %d/%d", syncedFiles, totalFiles);
  
  int progress = (syncedFiles * 16) / totalFiles;
  lcd.setCursor(2, 2);
  lcd.print("[");
  for (int i = 0; i < 16; i++) {
    if (i < progress) {
      lcd.print("=");
    } else {
      lcd.print(" ");
    }
  }
  lcd.print("]");
  
  int percentage = (syncedFiles * 100) / totalFiles;
  lcd.setCursor(6, 3);
  lcd.printf("%d%% Complete", percentage);
}

void displayWiFiStatus(bool connecting) {
  if (!lcdAvailable) return;
  
  lcd.clear();
  
  if (connecting) {
    lcd.setCursor(1, 0);
    lcd.print("Connecting WiFi...");
    lcd.setCursor(4, 2);
    lcd.print("Please wait");
  } else {
    if (wifiConnected) {
      lcd.setCursor(3, 0);
      lcd.print("WiFi Connected!");
      lcd.setCursor(0, 2);
      lcd.print("IP: ");
      lcd.print(WiFi.localIP().toString());
    } else {
      lcd.setCursor(3, 0);
      lcd.print("WiFi Failed!");
      lcd.setCursor(2, 1);
      lcd.print("Entering Offline");
      lcd.setCursor(6, 2);
      lcd.print("Mode");
    }
    delay(2000);
  }
}

void handleEmergencyWithLCD(String emergencyReason) {
  if (!lcdAvailable) return;
  
  lcd.clear();
  lcd.setCursor(2, 0);
  lcd.print("EMERGENCY STOP!");
  
  lcd.setCursor(0, 1);
  lcd.print("Reason:");
  
  if (emergencyReason.length() <= 20) {
    lcd.setCursor(0, 2);
    lcd.print(emergencyReason);
  } else {
    lcd.setCursor(0, 2);
    lcd.print(emergencyReason.substring(0, 20));
    if (emergencyReason.length() > 20) {
      lcd.setCursor(0, 3);
      lcd.print(emergencyReason.substring(20, min(40, (int)emergencyReason.length())));
    }
  }
  
  for (int i = 0; i < 10; i++) {
    lcd.noBacklight();
    delay(200);
    lcd.backlight();
    delay(200);
  }
  
  delay(30000);
}

bool connectToWiFiWithLCD() {
  Serial.println("Connecting to WiFi...");
  
  if (lcdAvailable) {
    displayWiFiStatus(true);
  }
  
  WiFi.begin(ssid, password);
  
  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - startTime) < WIFI_TIMEOUT_MS) {
    delay(500);
    Serial.print(".");
  }
  
  bool connected = (WiFi.status() == WL_CONNECTED);
  
  if (lcdAvailable) {
    displayWiFiStatus(false);
  }
  
  if (connected) {
    Serial.println();
    Serial.print("WiFi Connected! IP: ");
    Serial.println(WiFi.localIP());
    return true;
  } else {
    Serial.println();
    Serial.println("WiFi connection failed!");
    return false;
  }
}

// ========== DEKLARASI FUNGSI LCD ==========
bool initializeLCD();
void displayStartupScreen();
void displayMainPage();
void displayPowerPage();
void displayFlowPage();
void displaySystemPage();
void displayAlert(String alertType, String message);
void showLCDAlert(String alertType, String message);
void updateLCD();
void displayOfflineModeNotification();
void displayOnlineModeNotification();
void displaySyncProgress(int syncedFiles, int totalFiles);
void displayWiFiStatus(bool connecting);
void handleEmergencyWithLCD(String emergencyReason);
bool connectToWiFiWithLCD();

void setup() {
  Serial.begin(115200);
  delay(3000);

  Serial.println("=== ESP32 Irrigation System v2.1 (FORCED MODE) ===");
  Serial.println("⚠️⚠️⚠️ CRITICAL WARNING ⚠️⚠️⚠️");
  Serial.println("TANK SENSOR CHECKS ARE DISABLED!");
  Serial.println("System will irrigate regardless of tank levels");
  Serial.println("ENSURE TANKS ARE MANUALLY FILLED AT ALL TIMES");
  Serial.println("Risk of pump damage if tanks run dry!");
  Serial.println("=====================================");
  Serial.println();

  Serial.println("\n📋 Available Serial Commands:");
  Serial.println("   R - Reset irrigation flags");
  Serial.println("   D - Run SD card diagnostic");
  Serial.println("   V - View CSV files");
  Serial.println("   S - Manual sync CSV to database");
  Serial.println("=====================================\n");
  
  Serial.println("=== ESP32 Irrigation System v2.1 (Offline Mode) ===");
  Serial.println("🚀 Starting system initialization...");
  
  // Initialize offline support first
  
    initializeOfflineSupport();

  // Initialize LED pins
  pinMode(WIFI_LED_PIN, OUTPUT);
  pinMode(SYSTEM_LED_PIN, OUTPUT);
  digitalWrite(WIFI_LED_PIN, LOW);
  digitalWrite(SYSTEM_LED_PIN, LOW);
  Serial.println("✅ Status LEDs initialized");
  
  // Initialize I2C FIRST with explicit pins
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000);
  Serial.printf("✅ I2C initialized (SDA:%d, SCL:%d)\n", SDA_PIN, SCL_PIN);
  delay(500);
  
  scanI2CDevices();
  delay(1000);

  
  // Scan I2C devices for debugging
  scanI2CDevices();
  delay(1000);
  
  // Initialize RTC with enhanced error handling
  rtcAvailable = initializeRTC();

  // Initialize LCD
  lcdAvailable = initializeLCD();
    if (lcdAvailable) {
      displayStartupScreen();
    } 
  
    if (sdCardAvailable && rtcAvailable) {
    Serial.println("\n⏳ Waiting for SD card to stabilize...");
    delay(2000); // PENTING: Beri waktu SD card siap
    
    Serial.println("🧪 Testing CSV logging...");
    
    DateTime now = rtc.now();
    
    char dateBuffer[12];
    sprintf(dateBuffer, "%04d-%02d-%02d", now.year(), now.month(), now.day());
    String testDate = String(dateBuffer);
    
    char monthBuffer[8];
    sprintf(monthBuffer, "%04d-%02d", now.year(), now.month());
    String monthYear = String(monthBuffer);
    
    Serial.printf("📅 Date: %s, Month: %s\n", testDate.c_str(), monthYear.c_str());
    
    // Test simple write
    Serial.println("\n--- Testing simple file write ---");
    File testFile = SD.open("/test.txt", FILE_WRITE);
    if (testFile) {
      testFile.println("Test write OK");
      testFile.flush(); // TAMBAHKAN ini
      testFile.close();
      Serial.println("✅ Simple write test PASSED");
      
      delay(200); // Delay sebelum remove
      SD.remove("/test.txt");
      
      // Sekarang setup CSV files
      Serial.println("\n--- Creating CSV files ---");
      
      String sensorFile = "/sensor_data/" + testDate + ".csv";
      bool s1 = ensureCSVFile(sensorFile, getSensorDataCSVHeader());
      
      String irrigFile = "/irrigation_logs/" + testDate + ".csv";
      bool s2 = ensureCSVFile(irrigFile, getIrrigationLogCSVHeader());
      
      String sysFile = "/system_logs/" + monthYear + "_system.csv";
      bool s3 = ensureCSVFile(sysFile, getSystemLogCSVHeader());
      
      if (s1 && s2 && s3) {
        Serial.println("\n✅ All CSV files ready");
        delay(200);
        logSystemEventCSV("STARTUP", "Irrigation system started");
        Serial.println("✅ Startup event logged");
      } else {
        Serial.println("\n⚠️ Some CSV files failed, but system will continue");
      }
    } else {
      Serial.println("❌ Simple write test FAILED");
      Serial.println("⚠️ Disabling SD card logging");
      sdCardAvailable = false;
    }
  } else {
    if (!sdCardAvailable) {
      Serial.println("⚠️ SD Card not available - continuing without logging");
    }
    if (!rtcAvailable) {
      Serial.println("⚠️ RTC not available - timestamps may be incorrect");
    }
  }

  // Initialize DHT sensor
  dht.begin();
  Serial.println("✅ DHT22 initialized");
  
  // Initialize temperature sensors
  temperatureSensors.begin();
  int tempSensorCount = temperatureSensors.getDeviceCount();
  Serial.printf("✅ Found %d DS18B20 temperature sensor(s)\n", tempSensorCount);
  
  // Initialize BH1750 light sensor
  if (lightMeter.begin()) {
    lightSensorAvailable = true;
    Serial.println("✅ BH1750 light sensor initialized");
  } else {
    lightSensorAvailable = false;
    Serial.println("❌ BH1750 initialization failed");
  }
  
  // Initialize GPIO pins
  pinMode(PUMP_PIN, OUTPUT);
  pinMode(FAN_PIN, OUTPUT);
  pinMode(WATER_VALVE_PIN, OUTPUT);
  pinMode(VITAMIN_VALVE_PIN, OUTPUT);
  pinMode(FLOW_SENSOR_PIN, INPUT_PULLUP);

  setupUltrasonicSensors();
  
  // Set all outputs to safe state
  digitalWrite(PUMP_PIN, LOW);
  digitalWrite(FAN_PIN, LOW);
  digitalWrite(WATER_VALVE_PIN, LOW);
  digitalWrite(VITAMIN_VALVE_PIN, LOW);
  Serial.println("✅ All outputs set to OFF state");
  
  // Attach flow sensor interrupt
  attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN), flowPulseCounter, FALLING);
  Serial.println("✅ Flow sensor interrupt attached");
  
  // Connect to WiFi
  wifiConnected = connectToWiFiWithLCD();
  
  if (wifiConnected) {
    initializeNTP();
    exitOfflineMode();
  } else {
    Serial.println("⚠️ RTC not available and WiFi offline - no time source!");
    enterOfflineMode();
  }
  
  // System status summary
  Serial.println("\n📊 === System Status ===");
  Serial.printf("WiFi: %s\n", wifiConnected ? "✅ Connected" : "❌ Offline");
  Serial.printf("RTC: %s\n", rtcAvailable ? "✅ Available" : "❌ Failed");
  Serial.printf("Light Sensor: %s\n", lightSensorAvailable ? "✅ Available" : "❌ Failed");
  Serial.printf("SD Card: %s\n", sdCardAvailable ? "✅ Available" : "❌ Failed");
  Serial.printf("Temp Sensors: %d found\n", temperatureSensors.getDeviceCount());
  Serial.printf("Mode: %s\n", offlineMode ? "🔌 OFFLINE" : "🌐 ONLINE");
  if (offlineMode) {
    Serial.printf("Offline Logs: %d entries\n", offlineLogCount);
  }
  Serial.println("================================");
  
  // Initialize timing variables
  lastSensorRead = millis();
  lastDataSend = millis();
  lastAutoControl = millis();
  lastThresholdSync = millis();
  lastScheduleCheck = millis();
  lastWiFiCheck = millis();
  lastOfflineSync = millis();
  
  Serial.println("🎯 System ready! Starting main loop...");
}


// ========================================
// PERBAIKAN YANG DIPERLUKAN
// ========================================

void loop() {
  unsigned long currentTime = millis();

  // Manual commands
  if (Serial.available() > 0) {
    char cmd = Serial.read();
    if (cmd == 'r' || cmd == 'R') {
      Serial.println("\n*** MANUAL FLAG RESET ***");
      systemStatus.morningIrrigationDone = false;
      systemStatus.eveningIrrigationDone = false;
      Serial.println("Flags cleared\n");
    }
    else if (cmd == 'd' || cmd == 'D') {
      diagnosticSDCard();
    }
    else if (cmd == 'v' || cmd == 'V') {
      // View latest CSV file
      Serial.println("\n📊 Available CSV files:");
      Serial.println("Commands:");
      Serial.println("  v1 - View today's sensor data");
      Serial.println("  v2 - View today's irrigation log");
      Serial.println("  v3 - View system log");
      
      while (!Serial.available()) delay(10);
      String subcmd = Serial.readStringUntil('\n');
      subcmd.trim();
      
      DateTime now = rtc.now();
      String today = String(now.year()) + "-" + String(now.month()) + "-" + String(now.day());
      
      if (subcmd == "1") {
        viewCSVFile("/sensor_data/" + today + ".csv");
      } else if (subcmd == "2") {
        viewCSVFile("/irrigation_logs/" + today + ".csv");
      } else if (subcmd == "3") {
        viewCSVFile("/system_logs/" + String(now.year()) + "-" + String(now.month()) + "_system.csv");
      }
    }
    else if (cmd == 's' || cmd == 'S') {
      // Manual sync
      Serial.println("\n🔄 Manual sync initiated...");
      syncOfflineCSVToDatabase();
    }
  }

  // Manual reset flags
  if (Serial.available() > 0) {
    char cmd = Serial.read();
    if (cmd == 'r' || cmd == 'R') {
      Serial.println("\n*** MANUAL FLAG RESET ***");
      systemStatus.morningIrrigationDone = false;
      systemStatus.eveningIrrigationDone = false;
      Serial.println("Flags cleared. Next schedule check will trigger if within window.\n");
    }
  }
  
  // Time check periodic
  static unsigned long lastTimePrint = 0;
  if (currentTime - lastTimePrint >= 30000) {
    int year, month, day, hour, minute, second;
    if (getSystemTime(year, month, day, hour, minute, second)) {
      Serial.printf("\n[TIME CHECK] Current: %02d:%02d:%02d | Morning: %02d:%02d | Evening: %02d:%02d\n", 
                    hour, minute, second,
                    schedule.morningHour, schedule.morningMinute,
                    schedule.eveningHour, schedule.eveningMinute);
      Serial.printf("[FLAGS] Morning Done: %s | Evening Done: %s\n\n",
                    systemStatus.morningIrrigationDone ? "YES" : "NO",
                    systemStatus.eveningIrrigationDone ? "YES" : "NO");
    } else {
      Serial.println("\n[TIME CHECK] ERROR: No time source available!\n");
    }
    lastTimePrint = currentTime;
  }
  
  updateStatusLEDs();
  updateLCD();
  
  if (currentTime - lastSensorRead >= SENSOR_INTERVAL) {
    readSensorsImproved();
    lastSensorRead = currentTime;
  }
  
  if (currentTime - lastDataSend >= SEND_INTERVAL) {
    if (wifiConnected && !offlineMode) {
      sendSensorData();
    } else if (sdCardAvailable) {
      createOfflineLogEntry();
    }
    lastDataSend = currentTime;
  }
  
  // Auto control - biarkan fungsi handle sendiri
  if (currentTime - lastAutoControl >= CONTROL_INTERVAL) {
    performAutoControl();
    lastAutoControl = currentTime;
  }
  
  if (currentTime - lastWiFiCheck >= WIFI_RECONNECT_INTERVAL) {
    checkWiFiStatus();
    lastWiFiCheck = currentTime;
  }
  
  if (wifiConnected && !offlineMode && (currentTime - lastThresholdSync >= THRESHOLD_SYNC_INTERVAL)) {
    syncThresholds();
    syncSchedule();
    lastThresholdSync = currentTime;
  }

  static unsigned long lastPZEMDataFetch = 0;
  if (wifiConnected && !offlineMode && (currentTime - lastPZEMDataFetch >= 30000)) {
    fetchPZEMDataFromDatabase();
    lastPZEMDataFetch = currentTime;
  }
  
  // ✅ PERBAIKAN: Scheduled irrigation check dengan semua kondisi
  if ((rtcAvailable || ntpTimeSet) && 
      schedule.enabled && 
      systemStatus.autoMode && 
      (currentTime - lastScheduleCheck >= SCHEDULE_CHECK_INTERVAL)) {
    
    checkScheduledIrrigation();
    lastScheduleCheck = currentTime;
    
  } else if ((currentTime - lastScheduleCheck >= SCHEDULE_CHECK_INTERVAL)) {
    // Log mengapa schedule check di-skip
    if (!rtcAvailable && !ntpTimeSet) {
      Serial.println("[SCHEDULE] Skipped: No time source available");
    } else if (!schedule.enabled) {
      Serial.println("[SCHEDULE] Skipped: Schedule is DISABLED in settings");
    } else if (!systemStatus.autoMode) {
      Serial.println("[SCHEDULE] Skipped: Auto mode is DISABLED");
    }
    lastScheduleCheck = currentTime;
  }
  
  if (wifiConnected && !offlineMode) {
    checkDeviceControl();
  }

  static unsigned long lastPowerCheck = 0;
  if (currentTime - lastPowerCheck >= 300000) {
    checkPowerQuality();
    lastPowerCheck = currentTime;
  }
  
  if (wifiConnected && !offlineMode && sdCardAvailable && 
      (currentTime - lastOfflineSync >= OFFLINE_SYNC_INTERVAL)) {
    syncOfflineCSVToDatabase();
    lastOfflineSync = currentTime;
  }
  
  if (wifiConnected && !offlineMode) {
    syncNTPTime();
  }

  delay(1000);
}

void setupLCDIntegration() {
  // Tambahkan setelah scanI2CDevices(); dan sebelum initializeRTC();
  
  // Initialize LCD
  lcdAvailable = initializeLCD();
  if (lcdAvailable) {
    displayStartupScreen();
  }
}

void checkWiFiStatus() {
  bool currentWiFiStatus = (WiFi.status() == WL_CONNECTED);
  
  if (wifiConnected && !currentWiFiStatus) {
    Serial.println("⚠️ WiFi disconnected! Entering offline mode...");
    wifiConnected = false;
    enterOfflineMode();
  } else if (!wifiConnected && currentWiFiStatus) {
    Serial.println("✅ WiFi reconnected! Exiting offline mode...");
    wifiConnected = true;
    exitOfflineMode();
  } else if (!wifiConnected && !currentWiFiStatus) {
    // Try to reconnect
    Serial.println("🔄 Attempting WiFi reconnection...");
    if (connectToWiFi()) {
      wifiConnected = true;
      exitOfflineMode();
    }
  }
}

void readSensorsImproved() {
  // Flow rate calculation (tetap sama)
  if (millis() - flowLastTime > 1000) {
    flowRate = ((1000.0 / (millis() - flowLastTime)) * flowPulseCount) / 7.5;
    totalFlowVolume += (flowRate / 60.0);
    flowPulseCount = 0;
    flowLastTime = millis();
  }
  
  Serial.println("📊 === Sensor Readings (IMPROVED) ===");
  
  // Display current time and mode
  if (rtcAvailable) {
    DateTime now = rtc.now();
    Serial.printf("🕒 Time: %02d:%02d:%02d\n", now.hour(), now.minute(), now.second());
  }
  Serial.printf("📡 Mode: %s\n", offlineMode ? "🔌 OFFLINE" : "🌐 ONLINE");
  
  // DHT22 readings
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();
  
  // Soil moisture
  int soilMoistureRaw = analogRead(SOIL_MOISTURE_PIN);
  float soilMoisture = map(soilMoistureRaw, 0, 4095, 100, 0);
  
  // Tank levels dengan multiple readings dan averaging
  float waterDistances[3];
  float vitaminDistances[3];
  int validWaterReadings = 0;
  int validVitaminReadings = 0;
  
  // Ambil 3 pembacaan untuk setiap sensor dengan delay
  for (int i = 0; i < 3; i++) {
    waterDistances[i] = measureDistanceImproved(WATER_TANK_TRIG_PIN, WATER_TANK_ECHO_PIN);
    if (waterDistances[i] > 0) validWaterReadings++;
    
    delay(100);
    
    vitaminDistances[i] = measureDistanceImproved(VITAMIN_TANK_TRIG_PIN, VITAMIN_TANK_ECHO_PIN);
    if (vitaminDistances[i] > 0) validVitaminReadings++;
    
    delay(100);
  }
  
  // Hitung rata-rata dari pembacaan yang valid
  float avgWaterDistance = -1;
  float avgVitaminDistance = -1;
  
  if (validWaterReadings > 0) {
    float sum = 0;
    int count = 0;
    for (int i = 0; i < 3; i++) {
      if (waterDistances[i] > 0) {
        sum += waterDistances[i];
        count++;
      }
    }
    avgWaterDistance = sum / count;
  }
  
  if (validVitaminReadings > 0) {
    float sum = 0;
    int count = 0;
    for (int i = 0; i < 3; i++) {
      if (vitaminDistances[i] > 0) {
        sum += vitaminDistances[i];
        count++;
      }
    }
    avgVitaminDistance = sum / count;
  }
  
  float waterLevel = distanceToLevel(avgWaterDistance, WATER_TANK_HEIGHT);
  float vitaminLevel = distanceToLevel(avgVitaminDistance, VITAMIN_TANK_HEIGHT);
  
  // Temperature sensors
  temperatureSensors.requestTemperatures();
  float waterTemp = temperatureSensors.getTempCByIndex(0);
  float vitaminTemp = temperatureSensors.getTempCByIndex(1);
  
  // Light sensor
  float lightLevel = 0;
  if (lightSensorAvailable) {
    lightLevel = lightMeter.readLightLevel();
  }

  // Fetch PZEM data from database (online mode only)
  static unsigned long lastPZEMFetch = 0;
  if (!offlineMode && (millis() - lastPZEMFetch >= 10000)) { // Fetch setiap 10 detik
    fetchPZEMDataFromDatabase();
    lastPZEMFetch = millis();
  }
  
  // Display PZEM status in sensor summary
  Serial.printf("⚡ Power Status: V=%.1fV, I=%.3fA, P=%.1fW, F=%.1fHz\n", 
    systemStatus.lastVoltage, systemStatus.lastCurrent, 
    systemStatus.lastPower, systemStatus.lastFrequency);
  
  // Update system status (kecuali PZEM yang sudah diupdate di fungsi terpisah)
  if (!isnan(temperature)) systemStatus.lastTemperature = temperature;
  systemStatus.lastSoilMoisture = soilMoisture;
  if (waterLevel >= 0) systemStatus.lastWaterLevel = waterLevel;
  if (vitaminLevel >= 0) systemStatus.lastVitaminLevel = vitaminLevel;

  // Display readings
  Serial.printf("🌡️ Temperature: %s\n", isnan(temperature) ? "ERROR" : (String(temperature, 2) + "°C").c_str());
  Serial.printf("💧 Humidity: %s\n", isnan(humidity) ? "ERROR" : (String(humidity, 2) + "%").c_str());
  Serial.printf("🌱 Soil Moisture: %.2f%%\n", soilMoisture);
  
  // Debug info untuk ultrasonic sensors
  Serial.printf("🚰 Water Tank: %s", waterLevel >= 0 ? (String(waterLevel, 2) + "%").c_str() : "ERROR");
  if (avgWaterDistance > 0) {
    Serial.printf(" (Distance: %.2fcm, Valid readings: %d/3)", avgWaterDistance, validWaterReadings);
  }
  Serial.println();
  
  Serial.printf("💊 Vitamin Tank: %s", vitaminLevel >= 0 ? (String(vitaminLevel, 2) + "%").c_str() : "ERROR");
  if (avgVitaminDistance > 0) {
    Serial.printf(" (Distance: %.2fcm, Valid readings: %d/3)", avgVitaminDistance, validVitaminReadings);
  }
  Serial.println();
  
  Serial.printf("🌡️ Water Temp: %s\n", waterTemp != DEVICE_DISCONNECTED_C ? (String(waterTemp, 2) + "°C").c_str() : "ERROR");
  Serial.printf("🌡️ Vitamin Temp: %s\n", vitaminTemp != DEVICE_DISCONNECTED_C ? (String(vitaminTemp, 2) + "°C").c_str() : "ERROR");
  Serial.printf("💨 Flow Rate: %.2f L/min\n", flowRate);
  Serial.printf("📊 Total Volume: %.2f L\n", totalFlowVolume);
  
  // Display PZEM readings from system status (yang sudah diupdate di fungsi terpisah)
  Serial.printf("⚡ Voltage: %s\n", systemStatus.lastVoltage > 0 ? (String(systemStatus.lastVoltage, 2) + "V").c_str() : "ERROR");
  Serial.printf("⚡ Current: %s\n", systemStatus.lastCurrent >= 0 ? (String(systemStatus.lastCurrent, 3) + "A").c_str() : "ERROR");
  Serial.printf("⚡ Power: %s\n", systemStatus.lastPower >= 0 ? (String(systemStatus.lastPower, 2) + "W").c_str() : "ERROR");
  Serial.printf("⚡ Energy: %s\n", systemStatus.lastEnergy >= 0 ? (String(systemStatus.lastEnergy, 3) + "kWh").c_str() : "ERROR");
  Serial.printf("⚡ Frequency: %s\n", systemStatus.lastFrequency > 0 ? (String(systemStatus.lastFrequency, 1) + "Hz").c_str() : "ERROR");
  Serial.printf("⚡ Power Factor: %s\n", systemStatus.lastPowerFactor >= 0 ? String(systemStatus.lastPowerFactor, 3).c_str() : "ERROR");
  
  if (lightSensorAvailable) {
    Serial.printf("☀️ Light Level: %.2f lux\n", lightLevel);
  }
  
  Serial.printf("⚙️ System - Pump: %s, Fan: %s, Auto: %s\n", 
    systemStatus.pumpActive ? "ON" : "OFF",
    systemStatus.fanActive ? "ON" : "OFF",
    systemStatus.autoMode ? "ENABLED" : "DISABLED"
  );
}

void fetchPZEMDataFromDatabase() {
  if (!wifiConnected || offlineMode) return;
  
  HTTPClient http;
  http.setTimeout(10000);
  http.begin(String(supabaseUrl) + "/rest/v1/energy_readings?select=*&order=created_at.desc&limit=1");
  http.addHeader("apikey", supabaseKey);
  http.addHeader("Authorization", String("Bearer ") + supabaseKey);
  
  int httpResponseCode = http.GET();
  
  if (httpResponseCode == 200) {
    String response = http.getString();
    JsonDocument doc;
    
    if (deserializeJson(doc, response) == DeserializationError::Ok && doc.size() > 0) {
      JsonObject pzemData = doc[0];
      
      // Update system status dengan data dari database
      if (!pzemData["voltage"].isNull()) {
        systemStatus.lastVoltage = pzemData["voltage"];
      }
      if (!pzemData["current"].isNull()) {
        systemStatus.lastCurrent = pzemData["current"];
      }
      if (!pzemData["power"].isNull()) {
        systemStatus.lastPower = pzemData["power"];
      }
      if (!pzemData["energy"].isNull()) {
        systemStatus.lastEnergy = pzemData["energy"];
      }
      if (!pzemData["frequency"].isNull()) {
        systemStatus.lastFrequency = pzemData["frequency"];
      }
      if (!pzemData["power_factor"].isNull()) {
        systemStatus.lastPowerFactor = pzemData["power_factor"];
      }
      
      Serial.println("📊 PZEM data updated from database:");
      Serial.printf("   Voltage: %.1f V\n", systemStatus.lastVoltage);
      Serial.printf("   Current: %.3f A\n", systemStatus.lastCurrent);
      Serial.printf("   Power: %.1f W\n", systemStatus.lastPower);
      Serial.printf("   Energy: %.3f kWh\n", systemStatus.lastEnergy);
      Serial.printf("   Frequency: %.1f Hz\n", systemStatus.lastFrequency);
      Serial.printf("   Power Factor: %.3f\n", systemStatus.lastPowerFactor);
    } else {
      Serial.println("❌ No PZEM data found in database");
    }
  } else {
    Serial.printf("❌ Failed to fetch PZEM data: %d\n", httpResponseCode);
  }
  
  http.end();
}

void sendSensorData() {
  if (!wifiConnected || WiFi.status() != WL_CONNECTED || offlineMode) {
    Serial.println("📡 WiFi not available, logging to SD card...");
    if (sdCardAvailable) {
      createOfflineLogEntry();  // Ini sudah CSV format
    }
    return;
  }
  
  HTTPClient http;
  http.setTimeout(15000);
  http.begin(String(supabaseUrl) + "/rest/v1/sensor_readings");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Prefer", "return=minimal");
  http.addHeader("apikey", supabaseKey);
  http.addHeader("Authorization", String("Bearer ") + supabaseKey);
  
  // Create JSON payload (untuk database)
  JsonDocument doc;
  
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();
  int soilMoistureRaw = analogRead(SOIL_MOISTURE_PIN);
  float soilMoisture = map(soilMoistureRaw, 0, 4095, 0, 100);
  
  if (isnan(temperature)) {
    doc["temperature"] = JsonVariant();
  } else {
    doc["temperature"] = temperature;
  }
  
  if (isnan(humidity)) {
    doc["humidity"] = JsonVariant();
  } else {
    doc["humidity"] = humidity;
  }
  
  doc["soil_moisture"] = soilMoisture;
  
  if (systemStatus.lastWaterLevel >= 0) {
    doc["water_tank_level"] = systemStatus.lastWaterLevel;
  } else {
    doc["water_tank_level"] = JsonVariant();
  }
  
  if (systemStatus.lastVitaminLevel >= 0) {
    doc["vitamin_tank_level"] = systemStatus.lastVitaminLevel;
  } else {
    doc["vitamin_tank_level"] = JsonVariant();
  }
  
  doc["flow_rate"] = flowRate;
  doc["total_flow_volume"] = totalFlowVolume;
  
  if (lightSensorAvailable) {
    float lightLevel = lightMeter.readLightLevel();
    if (lightLevel >= 0) {
      doc["light_level"] = lightLevel;
    } else {
      doc["light_level"] = JsonVariant();
    }
  } else {
    doc["light_level"] = JsonVariant();
  }
  
  String jsonString;
  serializeJson(doc, jsonString);
  
  int httpResponseCode = http.POST(jsonString);
  
  if (httpResponseCode == 201) {
    Serial.println("✅ Data sent to database!");
    
    // BACKUP ke CSV juga saat online
    if (sdCardAvailable) {
      createOfflineLogEntry();
    }
  } else {
    Serial.printf("❌ Data send failed: %d\n", httpResponseCode);
    // Fallback to CSV
    if (sdCardAvailable) {
      createOfflineLogEntry();
    }
  }
  
  http.end();
}


void checkScheduledIrrigation() {
  static int callCount = 0;
  callCount++;
  
  Serial.printf("\n[SCHEDULE CALL #%d]\n", callCount);
  
  if (!schedule.enabled) {
    Serial.println("❌ Schedule disabled");
    return;
  }
  
  int currentYear, currentMonth, currentDay, currentHour, currentMinute, currentSecond;
  
  if (!getSystemTime(currentYear, currentMonth, currentDay, 
                     currentHour, currentMinute, currentSecond)) {
    Serial.println("[SCHEDULE] No time available");
    return;
  }
  
  // Debug output setiap kali dipanggil
  Serial.println("\n=== SCHEDULE CHECK ===");
  Serial.printf("Current: %04d-%02d-%02d %02d:%02d:%02d\n", 
                currentYear, currentMonth, currentDay, currentHour, currentMinute, currentSecond);
  Serial.printf("Date Type: %s (Day: %d)\n", (currentDay % 2 == 0) ? "EVEN" : "ODD", currentDay);
  Serial.printf("Morning: %02d:%02d (Done: %s)\n", 
                schedule.morningHour, schedule.morningMinute,
                systemStatus.morningIrrigationDone ? "YES" : "NO");
  Serial.printf("Evening: %02d:%02d (Done: %s)\n",
                schedule.eveningHour, schedule.eveningMinute,
                systemStatus.eveningIrrigationDone ? "YES" : "NO");
  
  // Calculate day of week
  struct tm timeStruct = {0};
  timeStruct.tm_year = currentYear - 1900;
  timeStruct.tm_mon = currentMonth - 1;
  timeStruct.tm_mday = currentDay;
  mktime(&timeStruct);
  int dayOfWeek = timeStruct.tm_wday;
  
  // Skip weekends if disabled
  if (!schedule.weekendMode && (dayOfWeek == 0 || dayOfWeek == 6)) {
    Serial.println("Skipped: Weekend mode disabled");
    Serial.println("======================\n");
    return;
  }
  
  // Reset flags between 00:00 - 00:59
  if (currentHour == 0) {
    if (systemStatus.morningIrrigationDone || systemStatus.eveningIrrigationDone) {
      systemStatus.morningIrrigationDone = false;
      systemStatus.eveningIrrigationDone = false;
      Serial.println("Daily flags RESET");
    }
  }
  
  // Convert to minutes since midnight for easier comparison
  int currentMinuteOfDay = currentHour * 60 + currentMinute;
  int morningMinuteOfDay = schedule.morningHour * 60 + schedule.morningMinute;
  int eveningMinuteOfDay = schedule.eveningHour * 60 + schedule.eveningMinute;
  
  // Calculate differences
  int morningDiff = abs(currentMinuteOfDay - morningMinuteOfDay);
  int eveningDiff = abs(currentMinuteOfDay - eveningMinuteOfDay);
  
  Serial.printf("Morning diff: %d min | Evening diff: %d min\n", morningDiff, eveningDiff);
  
  // Determine if today is even or odd date
  bool isEvenDate = (currentDay % 2 == 0);
  
  // MORNING CHECK - Window ±5 menit
  if (!systemStatus.morningIrrigationDone && morningDiff <= 5) {
    Serial.println(">>> MORNING IRRIGATION TRIGGERED <<<");
    Serial.printf("Scheduled: %02d:%02d | Actual: %02d:%02d | Diff: %d min\n",
                  schedule.morningHour, schedule.morningMinute,
                  currentHour, currentMinute, morningDiff);
    
    // LOGIC BARU: Tanggal genap → Vitamin, Tanggal ganjil → Water
    bool useVitamin = isEvenDate;
    
    Serial.printf("Date: %s → Using %s tank\n", 
                  isEvenDate ? "EVEN" : "ODD",
                  useVitamin ? "VITAMIN" : "WATER");
    Serial.println("======================\n");
    
    executeScheduledIrrigationV2("Morning scheduled irrigation", useVitamin);
    systemStatus.morningIrrigationDone = true;
    return;
  }
  
  // EVENING CHECK - Window ±5 menit
  if (!systemStatus.eveningIrrigationDone && eveningDiff <= 5) {
    Serial.println(">>> EVENING IRRIGATION TRIGGERED <<<");
    Serial.printf("Scheduled: %02d:%02d | Actual: %02d:%02d | Diff: %d min\n",
                  schedule.eveningHour, schedule.eveningMinute,
                  currentHour, currentMinute, eveningDiff);
    
    // LOGIC BARU: Sore hari SELALU water (tidak peduli tanggal)
    bool useVitamin = false;
    
    Serial.println("Evening irrigation → Always using WATER tank");
    Serial.println("======================\n");
    
    executeScheduledIrrigationV2("Evening scheduled irrigation", useVitamin);
    systemStatus.eveningIrrigationDone = true;
    return;
  }
  
  Serial.println("No schedule triggered");
  Serial.println("======================\n");
}


void executeScheduledIrrigationV2(String reason, bool useVitamin) {
  
  Serial.println("⚠️ FORCED IRRIGATION MODE: Ignoring tank sensor readings");
  Serial.printf("   Water Tank Reading: %.1f%%\n", systemStatus.lastWaterLevel);
  Serial.printf("   Vitamin Tank Reading: %.1f%%\n", systemStatus.lastVitaminLevel);
  Serial.printf("   Selected Tank: %s\n", useVitamin ? "VITAMIN" : "WATER");
  
  if (sdCardAvailable) {
    String logEntry = "SCHEDULED_IRRIGATION: " + reason + 
                     " - Tank: " + (useVitamin ? "Vitamin" : "Water") +
                     " - Sensor check bypassed - " + getCurrentTimeString();
    logToSDCard("/irrigation_logs/scheduled.log", logEntry);
  }
  
  // Panggil startIrrigationV2 dengan parameter yang benar
  startIrrigationV2(reason + " (SCHEDULED)", useVitamin);
}

void performAutoControl() {
  Serial.println("\n=== AUTO CONTROL SYSTEM CHECK ===");
  Serial.printf("Mode: %s | Auto: %s\n", 
                offlineMode ? "OFFLINE" : "ONLINE",
                systemStatus.autoMode ? "ENABLED" : "DISABLED");
  
  if (!systemStatus.autoMode) {
    Serial.println("Auto mode disabled, skipping control");
    return;
  }
  
  // Soil moisture control - SELALU GUNAKAN WATER TANK
  if (systemStatus.lastSoilMoisture < thresholds.minSoilMoisture && !systemStatus.pumpActive) {
    
    Serial.println("\n*** SOIL MOISTURE TRIGGER DETECTED ***");
    Serial.printf("Current Soil Moisture: %.1f%%\n", systemStatus.lastSoilMoisture);
    Serial.printf("Threshold: %.1f%%\n", thresholds.minSoilMoisture);
    Serial.printf("Deficit: %.1f%%\n", thresholds.minSoilMoisture - systemStatus.lastSoilMoisture);
    Serial.println("Initiating automatic irrigation...");
    Serial.println("Soil Moisture Trigger → Always using WATER tank\n");
    
    // PERUBAHAN: Soil moisture trigger selalu gunakan water (useVitamin = false)
    bool useVitamin = false;
    startIrrigationV2("FORCED: Low soil moisture", useVitamin);
    
  } else if (systemStatus.lastSoilMoisture > (thresholds.minSoilMoisture + 20.0) && systemStatus.pumpActive) {
    Serial.println("\n*** SOIL MOISTURE SATISFIED ***");
    Serial.printf("Current Soil Moisture: %.1f%%\n", systemStatus.lastSoilMoisture);
    Serial.printf("Target reached, stopping irrigation...\n\n");
    stopIrrigation("Soil moisture sufficient");
  }
  
  // Temperature control (tidak berubah)
  if (systemStatus.lastTemperature > thresholds.maxTemperature && !systemStatus.fanActive) {
    Serial.println("\n*** TEMPERATURE CONTROL ACTIVATED ***");
    Serial.printf("Temperature: %.1f°C (threshold: %.1f°C)\n", 
                  systemStatus.lastTemperature, thresholds.maxTemperature);
    startFan("High temperature detected");
  } else if (systemStatus.lastTemperature < (thresholds.maxTemperature - 5.0) && systemStatus.fanActive) {
    stopFan("Temperature normalized");
  }
  
  // Status summary
  Serial.println("Current Status:");
  Serial.printf("  Pump: %s | Fan: %s | Soil: %.1f%%\n", 
                systemStatus.pumpActive ? "ON" : "OFF",
                systemStatus.fanActive ? "ON" : "OFF",
                systemStatus.lastSoilMoisture);
  Serial.println("=================================\n");
  
  // Safety timeout
  if (systemStatus.pumpActive && (millis() - systemStatus.pumpStartTime) > MAX_IRRIGATION_TIME) {
    Serial.println("\n*** SAFETY TIMEOUT TRIGGERED ***");
    stopIrrigation("Safety timeout - maximum irrigation time reached");
  }
}

void startIrrigationV2(String reason, bool useVitamin) {
  digitalWrite(PUMP_PIN, HIGH);
  systemStatus.pumpActive = true;
  systemStatus.pumpStartTime = millis();
  lastIrrigationTime = millis();
  lastIrrigationTrigger = reason;
  
  // Set valve
  if (useVitamin) {
    digitalWrite(WATER_VALVE_PIN, LOW);
    digitalWrite(VITAMIN_VALVE_PIN, HIGH);
    systemStatus.waterValveActive = false;
    systemStatus.vitaminValveActive = true;
  } else {
    digitalWrite(WATER_VALVE_PIN, HIGH);
    digitalWrite(VITAMIN_VALVE_PIN, LOW);
    systemStatus.waterValveActive = true;
    systemStatus.vitaminValveActive = false;
  }
  
  // DETAILED NOTIFICATION
  Serial.println("\n╔═══════════════════════════════════════════════════════╗");
  Serial.println("║       AUTOMATIC IRRIGATION STARTED                    ║");
  Serial.println("╚═══════════════════════════════════════════════════════╝");
  
  if (reason.indexOf("Morning scheduled") >= 0) {
    Serial.println("  TRIGGER TYPE: SCHEDULED IRRIGATION (MORNING)");
  } else if (reason.indexOf("Evening scheduled") >= 0) {
    Serial.println("  TRIGGER TYPE: SCHEDULED IRRIGATION (EVENING)");
  } else if (reason.indexOf("moisture") >= 0 || reason.indexOf("FORCED: Low soil") >= 0) {
    Serial.println("  TRIGGER TYPE: SOIL MOISTURE CONTROL");
    Serial.printf("  Current: %.1f%% | Threshold: %.1f%%\n", 
                  systemStatus.lastSoilMoisture, thresholds.minSoilMoisture);
  }
  
  Serial.printf("  Tank Used: %s\n", useVitamin ? "VITAMIN" : "WATER");
  Serial.printf("  Soil Moisture: %.1f%%\n", systemStatus.lastSoilMoisture);
  Serial.printf("  Temperature: %.1f°C\n", systemStatus.lastTemperature);
  Serial.println("  Reason: " + reason);
  Serial.println("╚═══════════════════════════════════════════════════════╝\n");
  
  // LOG KE CSV
  if (sdCardAvailable) {
    logIrrigationEventCSV("START", reason, useVitamin ? "vitamin" : "water", 0);
  }
  
  // Jika online, log ke database juga
  if (!offlineMode) {
    logIrrigationEvent("start", reason, useVitamin ? "vitamin" : "water");
  }
  
  // LCD notification - PERBAIKAN: Hapus bagian yang error
  if (lcdAvailable) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("IRRIGATION STARTED");
    lcd.setCursor(0, 1);
    lcd.print("Tank: ");
    lcd.print(useVitamin ? "VITAMIN" : "WATER");
    lcd.setCursor(0, 2);
    lcd.printf("Soil: %.0f%%", systemStatus.lastSoilMoisture);
    lcd.setCursor(0, 3);
    lcd.print("Status: ACTIVE");
    delay(2000);
  }
}

void stopIrrigation(String reason) {
  // HITUNG duration PERTAMA KALI di sini
  unsigned long duration = (millis() - systemStatus.pumpStartTime) / 1000;
  
  digitalWrite(PUMP_PIN, LOW);
  digitalWrite(WATER_VALVE_PIN, LOW);
  digitalWrite(VITAMIN_VALVE_PIN, LOW);
  
  systemStatus.pumpActive = false;
  systemStatus.waterValveActive = false;
  systemStatus.vitaminValveActive = false;
  
  Serial.println("\n╔═══════════════════════════════════════════════════════╗");
  Serial.println("║       AUTOMATIC IRRIGATION STOPPED                    ║");
  Serial.println("╚═══════════════════════════════════════════════════════╝");
  Serial.println("  Stop Reason: " + reason);
  Serial.printf("  Duration: %lu seconds (%.1f minutes)\n", duration, duration / 60.0);
  Serial.println("  ───────────────────────────────────────────────────────");
  Serial.println("  ORIGINAL TRIGGER: " + lastIrrigationTrigger);
  
  if (lastIrrigationTrigger.indexOf("scheduled") >= 0) {
    Serial.println("  This was a SCHEDULED irrigation session");
  } else if (lastIrrigationTrigger.indexOf("moisture") >= 0) {
    Serial.println("  This was a SOIL MOISTURE triggered session");
  }
  
  Serial.println("  ───────────────────────────────────────────────────────");
  Serial.printf("  Final Soil Moisture: %.1f%%\n", systemStatus.lastSoilMoisture);
  Serial.printf("  Water Level: %.1f%%\n", systemStatus.lastWaterLevel);
  Serial.printf("  Vitamin Level: %.1f%%\n", systemStatus.lastVitaminLevel);
  
  float waterUsed = (flowRate * duration) / 60.0;
  if (waterUsed > 0) {
    Serial.printf("  Estimated Water Used: %.2f liters\n", waterUsed);
  }
  
  Serial.println("╚═══════════════════════════════════════════════════════╝\n");
  
  if (sdCardAvailable) {
    logIrrigationEventCSV("STOP", reason, 
                         systemStatus.waterValveActive ? "water" : "vitamin", 
                         duration);
  }
  
  if (!offlineMode) {
    logIrrigationEvent("stop", reason, systemStatus.waterValveActive ? "water" : "vitamin");
  }
  
  if (lcdAvailable) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("IRRIGATION STOPPED");
    lcd.setCursor(0, 1);
    lcd.printf("Duration: %lus", duration);
    lcd.setCursor(0, 2);
    lcd.printf("Soil now: %.0f%%", systemStatus.lastSoilMoisture);
    lcd.setCursor(0, 3);
    lcd.print(reason.substring(0, 20));
    delay(3000);
  }
}
void startFan(String reason) {
  digitalWrite(FAN_PIN, HIGH);
  systemStatus.fanActive = true;
  Serial.println("🌪️ FAN STARTED: " + reason);
  
  if (sdCardAvailable) {
    logSystemEventCSV("FAN_START", reason);
  }
}

void stopFan(String reason) {
  digitalWrite(FAN_PIN, LOW);
  systemStatus.fanActive = false;
  Serial.println("🔇 FAN STOPPED: " + reason);
  
  if (sdCardAvailable) {
    logSystemEventCSV("FAN_STOP", reason);
  }
}

void checkDeviceControl() {
  if (!wifiConnected || offlineMode) return;
  
  HTTPClient http;
  http.setTimeout(5000);
  http.begin(String(supabaseUrl) + "/rest/v1/device_control?select=*");
  http.addHeader("apikey", supabaseKey);
  http.addHeader("Authorization", String("Bearer ") + supabaseKey);
  
  int httpResponseCode = http.GET();
  
  if (httpResponseCode == 200) {
    String response = http.getString();
    JsonDocument doc;
    
    if (deserializeJson(doc, response) == DeserializationError::Ok) {
      for (JsonObject device : doc.as<JsonArray>()) {
        String deviceName = device["device_name"];
        bool isActive = device["is_active"];
        bool autoMode = device["auto_mode"];
        
        if (deviceName == "pump") {
          if (!autoMode) {
            systemStatus.autoMode = false;
            if (isActive && !systemStatus.pumpActive) {
              // PERUBAHAN: Manual control SELALU gunakan water tank
              bool useVitamin = false;
              Serial.println("Manual Control Trigger → Always using WATER tank");
              startIrrigationV2("Manual control", useVitamin);
            } else if (!isActive && systemStatus.pumpActive) {
              stopIrrigation("Manual stop");
            }
          } else {
            systemStatus.autoMode = true;
          }
        } else if (deviceName == "fan") {
          if (isActive && !systemStatus.fanActive) {
            startFan("Manual control");
          } else if (!isActive && systemStatus.fanActive) {
            stopFan("Manual stop");
          }
        }
      }
    }
  }
  
  http.end();
}

void syncThresholds() {
  Serial.println("🔄 === Syncing Thresholds ===");
  
  if (!wifiConnected || offlineMode) {
    Serial.println("❌ Sync skipped: WiFi not connected or offline mode");
    return;
  }
  
  Serial.println("📡 Requesting thresholds from database...");
  
  HTTPClient http;
  http.setTimeout(10000);
  http.begin(String(supabaseUrl) + "/rest/v1/thresholds?select=*&order=updated_at.desc&limit=1");
  http.addHeader("apikey", supabaseKey);
  http.addHeader("Authorization", String("Bearer ") + supabaseKey);
  
  int httpResponseCode = http.GET();
  Serial.printf("📊 HTTP Response Code: %d\n", httpResponseCode);
  
  if (httpResponseCode == 200) {
    String response = http.getString();
    Serial.printf("📋 Response length: %d bytes\n", response.length());
    Serial.println("📄 Raw response: " + response.substring(0, min(200, (int)response.length())) + "...");
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, response);
    
    if (error == DeserializationError::Ok) {
      Serial.printf("🔢 Documents found: %d\n", doc.size());
      
      if (doc.size() > 0) {
        JsonObject threshold = doc[0];
        
        // Tampilkan nilai lama
        Serial.println("📊 Old thresholds:");
        Serial.printf("   Max Temp: %.1f°C\n", thresholds.maxTemperature);
        Serial.printf("   Min Soil: %.1f%%\n", thresholds.minSoilMoisture);
        Serial.printf("   Low Water: %.1f%%\n", thresholds.lowWaterLevel);
        Serial.printf("   Low Vitamin: %.1f%%\n", thresholds.lowVitaminLevel);
        Serial.printf("   Min Light: %.1f lux\n", thresholds.minLightLevel);
        
        // Update values
        float newMaxTemp = threshold["max_temperature"] | thresholds.maxTemperature;
        float newMinSoil = threshold["min_soil_moisture"] | thresholds.minSoilMoisture;
        float newLowWater = threshold["low_water_level"] | thresholds.lowWaterLevel;
        float newLowVitamin = threshold["low_vitamin_level"] | thresholds.lowVitaminLevel;
        float newMinLight = threshold["min_light_level"] | thresholds.minLightLevel;
        
        // Check for changes
        bool hasChanges = (newMaxTemp != thresholds.maxTemperature ||
                          newMinSoil != thresholds.minSoilMoisture ||
                          newLowWater != thresholds.lowWaterLevel ||
                          newLowVitamin != thresholds.lowVitaminLevel ||
                          newMinLight != thresholds.minLightLevel);
        
        if (hasChanges) {
          thresholds.maxTemperature = newMaxTemp;
          thresholds.minSoilMoisture = newMinSoil;
          thresholds.lowWaterLevel = newLowWater;
          thresholds.lowVitaminLevel = newLowVitamin;
          thresholds.minLightLevel = newMinLight;
          
          // Save to EEPROM for offline use
          saveThresholdsToEEPROM();
          
          Serial.println("✅ Thresholds UPDATED from database:");
          Serial.printf("   Max Temp: %.1f°C\n", thresholds.maxTemperature);
          Serial.printf("   Min Soil: %.1f%%\n", thresholds.minSoilMoisture);
          Serial.printf("   Low Water: %.1f%%\n", thresholds.lowWaterLevel);
          Serial.printf("   Low Vitamin: %.1f%%\n", thresholds.lowVitaminLevel);
          Serial.printf("   Min Light: %.1f lux\n", thresholds.minLightLevel);
        } else {
          Serial.println("ℹ️ Thresholds unchanged - no update needed");
        }
      } else {
        Serial.println("⚠️ No threshold data found in database");
      }
    } else {
      Serial.printf("❌ JSON parsing failed: %s\n", error.c_str());
      Serial.println("📄 Response that failed: " + response);
    }
  } else {
    Serial.printf("❌ HTTP request failed with code: %d\n", httpResponseCode);
    if (httpResponseCode > 0) {
      String errorResponse = http.getString();
      Serial.println("📄 Error response: " + errorResponse);
    } else {
      Serial.println("💔 Network connection issue");
    }
  }
  
  http.end();
  Serial.println("🔄 Threshold sync completed\n");
}

void syncSchedule() {
  Serial.println("🔄 === Syncing Schedule ===");
  
  if (!wifiConnected || offlineMode) {
    Serial.println("❌ Sync skipped: WiFi not connected or offline mode");
    return;
  }
  
  Serial.println("📡 Requesting schedule from database...");
  
  HTTPClient http;
  http.setTimeout(10000);
  http.begin(String(supabaseUrl) + "/rest/v1/irrigation_schedule?select=*&order=updated_at.desc&limit=1");
  http.addHeader("apikey", supabaseKey);
  http.addHeader("Authorization", String("Bearer ") + supabaseKey);
  
  int httpResponseCode = http.GET();
  Serial.printf("📊 HTTP Response Code: %d\n", httpResponseCode);
  
  if (httpResponseCode == 200) {
    String response = http.getString();
    Serial.printf("📋 Response length: %d bytes\n", response.length());
    Serial.println("📄 Raw response: " + response.substring(0, min(200, (int)response.length())) + "...");
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, response);
    
    if (error == DeserializationError::Ok) {
      Serial.printf("🔢 Documents found: %d\n", doc.size());
      
      if (doc.size() > 0) {
        JsonObject scheduleData = doc[0];
        
        // Tampilkan nilai lama
        Serial.println("📊 Old schedule:");
        Serial.printf("   Enabled: %s\n", schedule.enabled ? "YES" : "NO");
        Serial.printf("   Morning: %02d:%02d\n", schedule.morningHour, schedule.morningMinute);
        Serial.printf("   Evening: %02d:%02d\n", schedule.eveningHour, schedule.eveningMinute);
        Serial.printf("   Weekend Mode: %s\n", schedule.weekendMode ? "YES" : "NO");
        Serial.printf("   Tank Rotation: %s\n", schedule.tankRotation ? "YES" : "NO");
        
        // Update values
        bool newEnabled = scheduleData["enabled"] | schedule.enabled;
        bool newWeekendMode = scheduleData["weekend_mode"] | schedule.weekendMode;
        bool newTankRotation = scheduleData["tank_rotation"] | schedule.tankRotation;
        
        // Parse morning time
        String morningTime = scheduleData["morning_time"] | "08:00:00";
        int newMorningHour = schedule.morningHour;
        int newMorningMinute = schedule.morningMinute;
        
        if (morningTime.length() >= 5) {
          newMorningHour = morningTime.substring(0, 2).toInt();
          newMorningMinute = morningTime.substring(3, 5).toInt();
        }
        
        // Parse evening time
        String eveningTime = scheduleData["evening_time"] | "18:00:00";
        int newEveningHour = schedule.eveningHour;
        int newEveningMinute = schedule.eveningMinute;
        
        if (eveningTime.length() >= 5) {
          newEveningHour = eveningTime.substring(0, 2).toInt();
          newEveningMinute = eveningTime.substring(3, 5).toInt();
        }
        
        // Check for changes
        bool hasChanges = (newEnabled != schedule.enabled ||
                          newWeekendMode != schedule.weekendMode ||
                          newTankRotation != schedule.tankRotation ||
                          newMorningHour != schedule.morningHour ||
                          newMorningMinute != schedule.morningMinute ||
                          newEveningHour != schedule.eveningHour ||
                          newEveningMinute != schedule.eveningMinute);
        
        if (hasChanges) {
          schedule.enabled = newEnabled;
          schedule.weekendMode = newWeekendMode;
          schedule.tankRotation = newTankRotation;
          schedule.morningHour = newMorningHour;
          schedule.morningMinute = newMorningMinute;
          schedule.eveningHour = newEveningHour;
          schedule.eveningMinute = newEveningMinute;
          
          // Save to EEPROM for offline use
          saveScheduleToEEPROM();
          
          Serial.println("✅ Schedule UPDATED from database:");
          Serial.printf("   Enabled: %s\n", schedule.enabled ? "YES" : "NO");
          Serial.printf("   Morning: %02d:%02d\n", schedule.morningHour, schedule.morningMinute);
          Serial.printf("   Evening: %02d:%02d\n", schedule.eveningHour, schedule.eveningMinute);
          Serial.printf("   Weekend Mode: %s\n", schedule.weekendMode ? "YES" : "NO");
          Serial.printf("   Tank Rotation: %s\n", schedule.tankRotation ? "YES" : "NO");
        } else {
          Serial.println("ℹ️ Schedule unchanged - no update needed");
        }
      } else {
        Serial.println("⚠️ No schedule data found in database");
      }
    } else {
      Serial.printf("❌ JSON parsing failed: %s\n", error.c_str());
      Serial.println("📄 Response that failed: " + response);
    }
  } else {
    Serial.printf("❌ HTTP request failed with code: %d\n", httpResponseCode);
    if (httpResponseCode > 0) {
      String errorResponse = http.getString();
      Serial.println("📄 Error response: " + errorResponse);
    } else {
      Serial.println("💔 Network connection issue");
    }
  }
  
  http.end();
  Serial.println("🔄 Schedule sync completed\n");
}

void checkPowerQuality() {
  if (systemStatus.lastVoltage <= 0) return;
  
  Serial.println("🔌 === Power Quality Check ===");
  
  // Voltage quality
  if (systemStatus.lastVoltage < 200.0) {
    Serial.println("⚠️ LOW VOLTAGE detected - may affect pump performance");
  } else if (systemStatus.lastVoltage > 250.0) {
    Serial.println("⚠️ HIGH VOLTAGE detected - risk to equipment");
  }
  
  // Power factor quality
  if (systemStatus.lastPowerFactor < 0.8 && systemStatus.lastPower > 100.0) {
    Serial.printf("⚠️ POOR POWER FACTOR: %.3f - consider power factor correction\n", 
      systemStatus.lastPowerFactor);
  }
  
  // Energy consumption calculation
  static float lastEnergyReading = 0;
  static unsigned long lastEnergyTime = 0;
  
  if (lastEnergyTime > 0 && systemStatus.lastEnergy != lastEnergyReading) {
    unsigned long timeDiff = (millis() - lastEnergyTime) / 1000 / 3600; // hours
    if (timeDiff > 0) {
      float energyUsed = systemStatus.lastEnergy - lastEnergyReading;
      Serial.printf("📊 Energy used in last period: %.3f kWh\n", energyUsed);
      
      if (sdCardAvailable) {
        String logEntry = "ENERGY_USAGE: " + String(energyUsed) + " kWh - " + getCurrentTimeString();
        logToSDCard("/system_logs/energy.log", logEntry);
      }
    }
  }
  
  lastEnergyReading = systemStatus.lastEnergy;
  lastEnergyTime = millis();
}

// System status display function
void displaySystemStatus() {
  Serial.println("\n🎯 === SYSTEM STATUS SUMMARY ===");
  Serial.printf("📡 Connection: %s\n", wifiConnected ? "🌐 ONLINE" : "🔌 OFFLINE");
  Serial.printf("💾 SD Card: %s\n", sdCardAvailable ? "✅ Available" : "❌ Not Available");
  Serial.printf("🕒 Time Source: %s\n", getTimeSource().c_str());
  
  if (rtcAvailable) {
    Serial.println("   🕒 Primary: RTC DS3231");
  }
  if (ntpTimeSet) {
    Serial.println("   🌐 Backup: NTP Server");
  }
  if (!rtcAvailable && !ntpTimeSet) {
    Serial.println("   ⚠️ WARNING: No time source available!");
  }
  
  Serial.printf("☀️ Light Sensor: %s\n", lightSensorAvailable ? "✅ Working" : "❌ Failed");
  
  if (offlineMode) {
    Serial.printf("📊 Offline Logs: %d entries stored\n", offlineLogCount);
    Serial.println("⚙️ Using last known settings from EEPROM");
  }
  
  Serial.printf("🌡️ Temperature Threshold: %.1f°C\n", thresholds.maxTemperature);
  Serial.printf("🌱 Soil Moisture Threshold: %.1f%%\n", thresholds.minSoilMoisture);
  Serial.printf("🚰 Water Tank Threshold: %.1f%%\n", thresholds.lowWaterLevel);
  Serial.printf("💊 Vitamin Tank Threshold: %.1f%%\n", thresholds.lowVitaminLevel);
  
  if (schedule.enabled) {
    Serial.printf("⏰ Morning Schedule: %02d:%02d\n", schedule.morningHour, schedule.morningMinute);
    Serial.printf("⏰ Evening Schedule: %02d:%02d\n", schedule.eveningHour, schedule.eveningMinute);
    Serial.printf("📅 Weekend Mode: %s\n", schedule.weekendMode ? "Enabled" : "Disabled");
    Serial.printf("🔄 Tank Rotation: %s\n", schedule.tankRotation ? "Enabled" : "Disabled");
  } else {
    Serial.println("⏰ Scheduled Irrigation: Disabled");
  }
  
  Serial.println("================================\n");
}


void cleanupOldLogs() {
  if (!sdCardAvailable) return;
  
  Serial.println("🧹 Cleaning up old log files...");
  
  // Get current date
  DateTime now = rtc.now();
  int currentDay = now.day();
  int currentMonth = now.month();
  int currentYear = now.year();
  
 
  File root = SD.open("/sensor_data");
  if (root) {
    File file = root.openNextFile();
    while (file) {
      String filename = file.name();
      if (filename.endsWith(".json")) {
        int year = filename.substring(0, 4).toInt();
        int month = filename.substring(5, filename.indexOf('-', 5)).toInt();
        int day = filename.substring(filename.lastIndexOf('-') + 1, filename.indexOf('.')).toInt();
        
        // Calculate age in days (simplified)
        int ageInDays = (currentYear - year) * 365 + (currentMonth - month) * 30 + (currentDay - day);
        
        if (ageInDays > 7) {
          String fullPath = "/sensor_data/" + filename;
          SD.remove(fullPath);
          Serial.printf("🗑️ Deleted old log: %s\n", filename.c_str());
        }
      }
      file = root.openNextFile();
    }
    root.close();
  }
}


void handleEmergencyOfflineOperation() {
  Serial.println("🚨 === EMERGENCY OFFLINE OPERATION ===");
  
  bool emergencyStop = false;
  String emergencyReason = "";
  
  
  
  if (systemStatus.lastTemperature > 45.0) {
    emergencyStop = true;
    emergencyReason += "Temperature critically high";
  }

  if (systemStatus.lastVoltage > 0 && (systemStatus.lastVoltage > 260.0 || systemStatus.lastVoltage < 180.0)) {
    emergencyStop = true;
    emergencyReason += (emergencyReason.length() > 0 ? " & " : "");
    emergencyReason += "Voltage out of safe range";
  }
  
  if (emergencyStop) {
    Serial.println("🚨 EMERGENCY STOP TRIGGERED: " + emergencyReason);
    
    digitalWrite(PUMP_PIN, LOW);
    digitalWrite(FAN_PIN, LOW);
    digitalWrite(WATER_VALVE_PIN, LOW);
    digitalWrite(VITAMIN_VALVE_PIN, LOW);
    
    systemStatus.pumpActive = false;
    systemStatus.fanActive = false;
    systemStatus.waterValveActive = false;
    systemStatus.vitaminValveActive = false;
    systemStatus.autoMode = false;
    
    if (sdCardAvailable) {
      String logEntry = "EMERGENCY_STOP: " + emergencyReason + " - " + getCurrentTimeString();
      logToSDCard("/system_logs/emergency.log", logEntry);
    }
    
    if (lcdAvailable) {
      handleEmergencyWithLCD(emergencyReason);
    }
  }
  
  
  Serial.println("ℹ️ Tank sensor checks DISABLED in emergency mode");
}

// Get SD card usage statistics
void printSDCardStats() {
  if (!sdCardAvailable) return;
  
  uint64_t totalBytes = SD.totalBytes();
  uint64_t usedBytes = SD.usedBytes();
  uint64_t freeBytes = totalBytes - usedBytes;
  
  Serial.println("💽 === SD Card Statistics ===");
  Serial.printf("Total: %.2f MB\n", totalBytes / (1024.0 * 1024.0));
  Serial.printf("Used: %.2f MB (%.1f%%)\n", usedBytes / (1024.0 * 1024.0), (usedBytes * 100.0) / totalBytes);
  Serial.printf("Free: %.2f MB\n", freeBytes / (1024.0 * 1024.0));
  
  // Count log files
  int sensorFiles = 0;
  int logFiles = 0;
  
  File root = SD.open("/sensor_data");
  if (root) {
    File file = root.openNextFile();
    while (file) {
      if (!file.isDirectory()) sensorFiles++;
      file = root.openNextFile();
    }
    root.close();
  }
  
  root = SD.open("/system_logs");
  if (root) {
    File file = root.openNextFile();
    while (file) {
      if (!file.isDirectory()) logFiles++;
      file = root.openNextFile();
    }
    root.close();
  }
  
  Serial.printf("📁 Sensor Data Files: %d\n", sensorFiles);
  Serial.printf("📁 System Log Files: %d\n", logFiles);
  Serial.println("===============================");
  

  if ((usedBytes * 100.0) / totalBytes > 80.0) {
    Serial.println("⚠️ Storage getting full, cleaning up old logs...");
    cleanupOldLogs();
  }
}


void initializeOfflineSupport() {
  Serial.println("🔧 === Initializing Offline Support ===");
  
  // Initialize EEPROM
  if (!EEPROM.begin(EEPROM_SIZE)) {
    Serial.println("❌ EEPROM failed!");
  } else {
    Serial.println("✅ EEPROM ready (512 bytes)");
  }
  
  // Initialize SD Card
  sdCardAvailable = initializeSDCard();
  
  if (!sdCardAvailable) {
    Serial.println("⚠️ SD Card unavailable - logging disabled");
  }
  
  // Load settings
  loadThresholdsFromEEPROM();
  loadScheduleFromEEPROM();
  
  Serial.println("✅ Offline support initialized");
  
  if (sdCardAvailable) {
    printSDCardStats();
    delay(500);
  }
}

void setupUltrasonicSensors() {

  pinMode(WATER_TANK_TRIG_PIN, OUTPUT);
  pinMode(WATER_TANK_ECHO_PIN, INPUT);
  pinMode(VITAMIN_TANK_TRIG_PIN, OUTPUT);  
  pinMode(VITAMIN_TANK_ECHO_PIN, INPUT);
  
 
  digitalWrite(WATER_TANK_TRIG_PIN, LOW);
  digitalWrite(VITAMIN_TANK_TRIG_PIN, LOW);
  
  
  delay(1000);
  
  Serial.println("✅ Ultrasonic sensors (SRF05/HC-SR04) initialized");
  Serial.printf("   Water Tank: TRIG=%d, ECHO=%d\n", WATER_TANK_TRIG_PIN, WATER_TANK_ECHO_PIN);
  Serial.printf("   Vitamin Tank: TRIG=%d, ECHO=%d\n", VITAMIN_TANK_TRIG_PIN, VITAMIN_TANK_ECHO_PIN);
  
  
  delay(1000);
  testUltrasonicSensors();
}

void testUltrasonicSensors() {
  Serial.println("=== TESTING ULTRASONIC SENSORS ===");
  
  
  Serial.println("Testing Water Tank Sensor (SRF05):");
  for (int i = 0; i < 5; i++) {
    float waterDistance = measureDistanceImproved(WATER_TANK_TRIG_PIN, WATER_TANK_ECHO_PIN);
    if (waterDistance > 0) {
      float waterLevel = distanceToLevel(waterDistance, WATER_TANK_HEIGHT);
      Serial.printf("  Reading %d: Distance=%.2fcm, Level=%.1f%%\n", 
                    i+1, waterDistance, waterLevel);
    } else {
      Serial.printf("  Reading %d: ERROR\n", i+1);
    }
    delay(200);
  }
  
  Serial.println();
  
  
  Serial.println("Testing Vitamin Tank Sensor (SRF05):");
  for (int i = 0; i < 5; i++) {
    float vitaminDistance = measureDistanceImproved(VITAMIN_TANK_TRIG_PIN, VITAMIN_TANK_ECHO_PIN);
    if (vitaminDistance > 0) {
      float vitaminLevel = distanceToLevel(vitaminDistance, VITAMIN_TANK_HEIGHT);
      Serial.printf("  Reading %d: Distance=%.2fcm, Level=%.1f%%\n", 
                    i+1, vitaminDistance, vitaminLevel);
    } else {
      Serial.printf("  Reading %d: ERROR\n", i+1);
    }
    delay(200);
  }
  
  Serial.println("================================");
}
