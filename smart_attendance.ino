/*
 * Smart Attendance System - ESP32 (Offline Master Mode)
 * 
 * This system works FULLY OFFLINE with local storage
 * Mobile app is only used for:
 * - Adding/Editing student data
 * - Viewing detailed statistics
 * - Downloading attendance logs
 */
#include <nvs_flash.h>
#include <nvs.h>
// ... your other imports

#include <esp_wifi.h>
#include <esp_mac.h>

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <MFRC522.h>
#include <SPI.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Preferences.h>
#include <ArduinoJson.h>

// ==================== PIN DEFINITIONS ====================
#define SS_PIN          5
#define RST_PIN         4
#define BUZZER_PIN      25
#define BTN_MODE        33
#define BTN_CLEAR       32
#define I2C_SDA         21
#define I2C_SCL         22
#define LED_PIN         2

// ==================== BLE CONFIGURATION ====================
#define SERVICE_UUID            "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHAR_SCAN_UUID          "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define CHAR_STUDENT_RX_UUID    "a07498ca-ad5b-474e-940d-16f1fbe7e8cd"
#define CHAR_STUDENT_TX_UUID    "51ff12bb-3ed8-46e5-b4f9-d64e2fec021b"
#define CHAR_STATS_UUID         "0972ef8c-7613-4075-ad52-756f33d4da91"
#define CHAR_LOGS_UUID          "9a8ca5b5-6f3d-4b0e-8b3e-4c8b8e8a5f2d"
#define CHAR_COMMAND_UUID       "7e7d1c3b-8c5e-4b1f-9d2e-5a6b7c8d9e0f"

BLEServer* pServer = NULL;
BLECharacteristic* pCharScan = NULL;
BLECharacteristic* pCharStudentRx = NULL;
BLECharacteristic* pCharStudentTx = NULL;
BLECharacteristic* pCharStats = NULL;
BLECharacteristic* pCharLogs = NULL;
BLECharacteristic* pCharCommand = NULL;
bool deviceConnected = false;

// ==================== HARDWARE OBJECTS ====================
MFRC522 rfid(SS_PIN, RST_PIN);
LiquidCrystal_I2C lcd(0x27, 16, 2);
Preferences preferences;

// ==================== STUDENT STRUCTURE ====================
struct Student {
  char uid[20];
  char name[50];
  char studentClass[20];
  bool active;
};

// ==================== SYSTEM VARIABLES ====================
enum SystemMode {
  MODE_ATTENDANCE,
  MODE_STATS,
  MODE_VIEW_STUDENTS,
  MODE_SYNC
};

SystemMode currentMode = MODE_ATTENDANCE;
unsigned long lastCardScan = 0;
unsigned long lastButtonPress = 0;
unsigned long lastLCDUpdate = 0;
unsigned long lastStatsUpdate = 0;
const unsigned long SCAN_DELAY = 2000;
const unsigned long DEBOUNCE_DELAY = 300;
const unsigned long LCD_UPDATE_INTERVAL = 1000;
const unsigned long STATS_UPDATE_INTERVAL = 60000;

// Statistics
int totalStudents = 0;
int todayPresent = 0;
int totalScansToday = 0;
int daySession = 0;
String currentDate = "";
byte lcdFrame = 0;
String lastScannedName = "No Scans Yet";
// ==================== CUSTOM LCD CHARACTERS ====================
byte checkMark[8] = {0b00000, 0b00001, 0b00011, 0b10110, 0b11100, 0b01000, 0b00000, 0b00000};
byte heart[8] = {0b00000, 0b01010, 0b11111, 0b11111, 0b11111, 0b01110, 0b00100, 0b00000};
byte wifi[8] = {0b00000, 0b01110, 0b10001, 0b00100, 0b01010, 0b00000, 0b00100, 0b00000};
byte personIcon[8] = {0b01110, 0b01110, 0b00100, 0b01110, 0b10101, 0b00100, 0b01010, 0b10001};

// ==================== FORWARD DECLARATIONS ====================
void playTone(int frequency, int duration);
void playBeep();
void playSuccessTone();
void playErrorTone();
void playEnrollTone();
void playReadyTone();
void addOrUpdateStudent(const char* uid, const char* name, const char* studentClass);
void deleteStudent(const char* uid);
void sendResponse(const char* response);
void sendAllStudents();
void sendStatistics();
void sendAttendanceLogs();
void clearTodayAttendance();
void clearAllData();
void displayAttendanceScreen();
String getCurrentDate();
String getCurrentTime();

// ==================== BUZZER FUNCTIONS (Must be before callbacks) ====================
void playTone(int frequency, int duration) {
  tone(BUZZER_PIN, frequency, duration);
  delay(duration);
  noTone(BUZZER_PIN);
}

void playBeep() {
  playTone(2000, 100);
}

void playSuccessTone() {
  playTone(1000, 100);
  delay(50);
  playTone(1500, 100);
  delay(50);
  playTone(2000, 150);
}

void playErrorTone() {
  playTone(500, 200);
  delay(100);
  playTone(400, 200);
  delay(100);
  playTone(300, 300);
}

void playEnrollTone() {
  playTone(1500, 100);
  delay(50);
  playTone(1800, 100);
  delay(50);
  playTone(2100, 100);
  delay(50);
  playTone(2400, 200);
}

void playReadyTone() {
  playTone(1000, 100);
  delay(100);
  playTone(2000, 100);
}

// ==================== BLE CALLBACKS ====================
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      Serial.println("✓ Device connected!");
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Phone Connected!");
      lcd.setCursor(0, 1);
      lcd.write(2);
      lcd.print(" Sync Mode");
      playTone(2000, 100);
      delay(100);
      playTone(2500, 100);
      digitalWrite(LED_PIN, HIGH);
      delay(1500);
    }

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      Serial.println("✗ Device disconnected!");
      digitalWrite(LED_PIN, LOW);
      delay(500);
      BLEDevice::startAdvertising();
      currentMode = MODE_ATTENDANCE;
    }
};

class StudentDataCallback: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      String value = pCharacteristic->getValue().c_str();
      
      if (value.length() > 0) {
        Serial.println("📥 Received student data");
        
        StaticJsonDocument<512> doc;
        DeserializationError error = deserializeJson(doc, value);
        
        if (error) {
          Serial.print("JSON Parse Error: ");
          Serial.println(error.c_str());
          sendResponse("ERROR: Invalid JSON");
          return;
        }
        
        const char* action = doc["action"];
        const char* uid = doc["uid"];
        const char* name = doc["name"];
        const char* studentClass = doc["class"];
        
        if (strcmp(action, "add") == 0 || strcmp(action, "update") == 0) {
          addOrUpdateStudent(uid, name, studentClass);
          sendResponse("SUCCESS");
        } else if (strcmp(action, "delete") == 0) {
          deleteStudent(uid);
          sendResponse("SUCCESS");
        }
      }
    }
};

class CommandCallback: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      String value = pCharacteristic->getValue().c_str();
      
      if (value.length() > 0) {
        Serial.print("📡 Command received: ");
        Serial.println(value);
        
        if (value == "GET_STUDENTS") {
          sendAllStudents();
        } else if (value == "GET_STATS") {
          sendStatistics();
        } else if (value == "GET_LOGS") {
          sendAttendanceLogs();
        } else if (value == "CLEAR_TODAY") {
          clearTodayAttendance();
        } else if (value == "CLEAR_ALL") {
          clearAllData();
        }
      }
    }
};

// ==================== DATE/TIME FUNCTIONS ====================
String getCurrentDate() {
  unsigned long days = millis() / 86400000;
  return String("2024-01-") + String(days % 31 + 1);
}

String getCurrentTime() {
  unsigned long seconds = (millis() / 1000) % 86400;
  int hours = seconds / 3600;
  int minutes = (seconds % 3600) / 60;
  int secs = seconds % 60;
  
  char timeStr[10];
  sprintf(timeStr, "%02d:%02d:%02d", hours, minutes, secs);
  return String(timeStr);
}

// ==================== STUDENT MANAGEMENT ====================
void addOrUpdateStudent(const char* uid, const char* name, const char* studentClass) {
  String key = String("stu_") + uid;
  
  StaticJsonDocument<256> doc;
  doc["name"] = name;
  doc["class"] = studentClass;
  doc["active"] = true;
  
  String jsonString;
  serializeJson(doc, jsonString);
  
  bool isNew = !preferences.isKey(key.c_str());
  
  preferences.putString(key.c_str(), jsonString);
  
  if (isNew) {
    totalStudents++;
    preferences.putInt("totalStudents", totalStudents);
  }
  
  Serial.print(isNew ? "➕ Added: " : "✏️  Updated: ");
  Serial.print(name);
  Serial.print(" (");
  Serial.print(uid);
  Serial.println(")");
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(isNew ? "Student Added!" : "Student Updated!");
  lcd.setCursor(0, 1);
  lcd.print(name);
  
  playEnrollTone();
  delay(2000);
  displayAttendanceScreen();
}

void deleteStudent(const char* uid) {
  String key = String("stu_") + uid;
  
  if (preferences.isKey(key.c_str())) {
    preferences.remove(key.c_str());
    totalStudents--;
    preferences.putInt("totalStudents", totalStudents);
    
    Serial.print("🗑️  Deleted student: ");
    Serial.println(uid);
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Student Deleted");
    playTone(500, 200);
    delay(2000);
    displayAttendanceScreen();
  }
}

Student getStudent(const char* uid) {
  Student student;
  strcpy(student.uid, "");
  
  String key = String("stu_") + uid;
  
  if (preferences.isKey(key.c_str())) {
    String jsonString = preferences.getString(key.c_str(), "");
    
    StaticJsonDocument<256> doc;
    deserializeJson(doc, jsonString);
    
    strcpy(student.uid, uid);
    strcpy(student.name, doc["name"]);
    strcpy(student.studentClass, doc["class"]);
    student.active = doc["active"];
  }
  
  return student;
}

void sendAllStudents() {
  Serial.println("📤 Sending all students...");
  
  StaticJsonDocument<4096> doc;
  JsonArray students = doc.createNestedArray("students");
  
  // NEW API: Use pointer for iterator & check result code
  nvs_iterator_t it = NULL;
  esp_err_t res = nvs_entry_find("nvs", "attendance", NVS_TYPE_STR, &it);
  
  while (res == ESP_OK) {
    nvs_entry_info_t info;
    nvs_entry_info(it, &info);
    
    String key = String(info.key);
    
    // If this key is a Student
    if (key.startsWith("stu_")) {
      String jsonString = preferences.getString(key.c_str(), "");
      if (jsonString.length() > 0) {
        StaticJsonDocument<256> sDoc;
        DeserializationError error = deserializeJson(sDoc, jsonString);
        
        if (!error) {
          JsonObject s = students.createNestedObject();
          s["uid"] = key.substring(4); // Remove "stu_"
          s["name"] = sDoc["name"];
          s["class"] = sDoc["class"];
          s["active"] = sDoc["active"];
        }
      }
    }
    // NEW API: Advance iterator by passing address
    res = nvs_entry_next(&it);
  }
  nvs_release_iterator(it);
  
  String out;
  serializeJson(doc, out);
  
  pCharStudentTx->setValue(out.c_str());
  pCharStudentTx->notify();
  
  Serial.print("✓ Sent students: ");
  Serial.println(students.size());
}

void sendResponse(const char* response) {
  pCharStudentRx->setValue(response);
  pCharStudentRx->notify();
}

// ==================== ATTENDANCE LOGGING ====================
void logAttendance(const char* uid, const char* name) {
  String date = getCurrentDate();
  
  // FIX 1: Use a SHORT key (max 15 chars). "a_" + 8 chars of UID = 10 chars. Safe!
  String shortKey = String("a_") + uid;
  String sessionToken = date + "_" + String(daySession);
  
  // Check if already marked for THIS date and THIS session
  if (preferences.isKey(shortKey.c_str())) {
    String lastMarked = preferences.getString(shortKey.c_str(), "");
    if (lastMarked == sessionToken) {
      Serial.println("⚠️  Already marked today");
      
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Already Marked!");
      lcd.setCursor(0, 1);
      lcd.print(name);
      playTone(1500, 200);
      delay(2500); // Delay for "Already Marked" message
      displayAttendanceScreen();
      return;
    }
  }
  
  // Mark Attendance
  unsigned long timestamp = millis();
  preferences.putString(shortKey.c_str(), sessionToken); // Save new token
  
  todayPresent++;
  String countKey = String("count_") + date;
  preferences.putInt(countKey.c_str(), todayPresent);
  
  totalScansToday++;
  
  lastScannedName = String(name);

  Serial.print("✓ Attendance logged: ");
  Serial.println(name);
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.write(0); // Checkmark
  lcd.print(" ");
  lcd.print(name);
  lcd.setCursor(0, 1);
  lcd.print("Present: ");
  lcd.print(todayPresent);
  
  playSuccessTone();
  
  // FIX 2: Increased Delay to 3 Seconds
  delay(3000); 
  
  displayAttendanceScreen();

  // Send to Phone
  if (deviceConnected) {
    StaticJsonDocument<256> doc;
    doc["uid"] = uid;
    doc["name"] = name;
    doc["timestamp"] = timestamp;
    
    String jsonString;
    serializeJson(doc, jsonString);
    
    pCharScan->setValue(jsonString.c_str());
    pCharScan->notify();
    
    // FIX 3: Force update stats on phone immediately
    sendStatistics();
  }
}
// ==================== RFID FUNCTIONS ====================
String getCardUID() {
  String uid = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) uid += "0";
    uid += String(rfid.uid.uidByte[i], HEX);
  }
  uid.toUpperCase();
  return uid;
}

void handleCardScan(String uid) {
  Serial.println("\n━━━━━━━━━━━━━━━━━━━━");
  Serial.print("📇 Card: ");
  Serial.println(uid);

  // 1. Try to find the student in ESP32 memory FIRST
  Student student = getStudent(uid.c_str());
  bool studentExists = (strlen(student.uid) > 0);

  // 2. If Student Exists -> Mark Attendance (Saves to ESP32 & Notifies Phone)
  if (studentExists) {
    logAttendance(student.uid, student.name);
    return; 
  }

  // 3. If Student Does NOT Exist -> Send Raw UID to Phone (For Registration)
  if (deviceConnected) {
    Serial.println("📱 Unknown card - sending to phone for registration");
    // Send raw UID so the "Add Student" screen can pick it up
    pCharScan->setValue(uid.c_str());
    pCharScan->notify();
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Card Scanned");
    lcd.setCursor(0, 1);
    lcd.print("Not Registered");
    playBeep();
    delay(1500);
    displayAttendanceScreen();
    return;
  }

  // 4. Offline & Unknown Card
  Serial.println("❌ Student not found (Offline mode)");
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Unknown Card!");
  playErrorTone();
  delay(2000);
  displayAttendanceScreen();
}
// ==================== STATISTICS ====================
void updateStatistics() {
  String date = getCurrentDate();
  String countKey = String("count_") + date;
  todayPresent = preferences.getInt(countKey.c_str(), 0);
}

void sendStatistics() {
  Serial.println("📊 Sending statistics...");
  
  StaticJsonDocument<512> doc;
  doc["totalStudents"] = totalStudents;
  doc["todayPresent"] = todayPresent;
  doc["todayAbsent"] = totalStudents - todayPresent;
  doc["date"] = getCurrentDate();
  doc["uptime"] = millis() / 1000;
  
  String jsonString;
  serializeJson(doc, jsonString);
  
  pCharStats->setValue(jsonString.c_str());
  pCharStats->notify();
  
  Serial.println("✓ Stats sent");
}

void sendAttendanceLogs() {
  Serial.println("📋 Sending attendance logs...");
  
  StaticJsonDocument<4096> doc;
  JsonArray logs = doc.createNestedArray("logs");
  
  String currentSessionToken = getCurrentDate() + "_" + String(daySession);
  
  // NEW API: Use pointer for iterator & check result code
  nvs_iterator_t it = NULL;
  esp_err_t res = nvs_entry_find("nvs", "attendance", NVS_TYPE_STR, &it);
  
  while (res == ESP_OK) {
    nvs_entry_info_t info;
    nvs_entry_info(it, &info);
    
    String key = String(info.key);
    
    // If this key is an Attendance Record (starts with "a_")
    if (key.startsWith("a_")) {
      String storedToken = preferences.getString(key.c_str(), "");
      
      // Only send if it matches the CURRENT session/date
      if (storedToken == currentSessionToken) {
        JsonObject l = logs.createNestedObject();
        l["uid"] = key.substring(2); // Remove "a_" prefix
        l["timestamp"] = 0; 
      }
    }
    // NEW API: Advance iterator by passing address
    res = nvs_entry_next(&it);
  }
  nvs_release_iterator(it);
  
  String out;
  serializeJson(doc, out);
  
  pCharLogs->setValue(out.c_str());
  pCharLogs->notify();
  
  Serial.print("✓ Sent logs: ");
  Serial.println(logs.size());
}

// ==================== LCD DISPLAYS ====================
void displayAttendanceScreen() {
  // Update statistics before displaying
  updateStatistics();
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Ready to Scan");
  lcd.setCursor(0, 1);
  lcd.print("P:");
  lcd.print(todayPresent);
  lcd.print(" A:");
  lcd.print(totalStudents - todayPresent);
  lcd.print(" T:");
  lcd.print(totalStudents);
}
void displayStatsScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Last Attended:"); // Title
  
  lcd.setCursor(0, 1);
  // Check if name is too long for the 16-character screen
  if (lastScannedName.length() > 16) {
    lcd.print(lastScannedName.substring(0, 16)); // Cut it to fit
  } else {
    lcd.print(lastScannedName);
  }
}

void displayStudentsScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.write(3);
  lcd.print(" Students: ");
  lcd.print(totalStudents);
  lcd.setCursor(0, 1);
  lcd.print("Use app to view");
}



void updateLCDAnimation() {
  if (currentMode == MODE_ATTENDANCE && !deviceConnected) {
    lcdFrame = (lcdFrame + 1) % 4;
    lcd.setCursor(15, 1);
    switch (lcdFrame) {
      case 0: lcd.print("|"); break;
      case 1: lcd.print("/"); break;
      case 2: lcd.print("-"); break;
      case 3: lcd.print("\\"); break;
    }
  }
}

void updateDateCheck() {
  String newDate = getCurrentDate();
  if (newDate != currentDate) {
    currentDate = newDate;
    todayPresent = 0;
    totalScansToday = 0;
    Serial.println("📅 New day started: " + currentDate);
  }
}

// ==================== BUTTON HANDLERS ====================
void handleButtons() {
  if (digitalRead(BTN_MODE) == LOW && millis() - lastButtonPress > DEBOUNCE_DELAY) {
    cycleMode();
    lastButtonPress = millis();
  }
  
  if (digitalRead(BTN_CLEAR) == LOW && millis() - lastButtonPress > DEBOUNCE_DELAY) {
    unsigned long pressStart = millis();
    while (digitalRead(BTN_CLEAR) == LOW && millis() - pressStart < 3000) {
      delay(10);
    }
    if (millis() - pressStart >= 3000) {
      clearTodayAttendance();
    }
    lastButtonPress = millis();
  }
}

void cycleMode() {
  playBeep();
  
  switch (currentMode) {
    case MODE_ATTENDANCE:
      currentMode = MODE_STATS;
      displayStatsScreen();
      break;
    case MODE_STATS:
      currentMode = MODE_VIEW_STUDENTS;
      displayStudentsScreen();
      break;
    case MODE_VIEW_STUDENTS:
      currentMode = MODE_ATTENDANCE;
      displayAttendanceScreen();
      break;
    default:
      currentMode = MODE_ATTENDANCE;
      displayAttendanceScreen();
  }
}

// ==================== DATA MANAGEMENT ====================
void clearTodayAttendance() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Clearing Today");
  
  // Reset counts
  String date = getCurrentDate();
  String countKey = String("count_") + date;
  preferences.putInt(countKey.c_str(), 0);
  todayPresent = 0;
  totalScansToday = 0;
  
  lastScannedName = "Cleared";

  // Increment Session (Invalidates previous "Already Marked" checks for today)
  daySession++;
  preferences.putInt("daySession", daySession);
  
  playErrorTone();
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Today Cleared!");
  delay(2000);
  displayAttendanceScreen();
  
  Serial.println("🗑️  Today's attendance cleared");

  // FIX: Sync with Phone immediately
  if (deviceConnected) {
    sendStatistics();
    sendAttendanceLogs(); // Clears the list on phone
  }
}

void clearAllData() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("FACTORY RESET");
  lcd.setCursor(0, 1);
  lcd.print("Clearing...");
  
  preferences.clear();
  totalStudents = 0;
  todayPresent = 0;
  totalScansToday = 0;
  daySession = 0; // Reset session
  
  for (int i = 0; i < 3; i++) {
    playTone(500 - i * 100, 200);
    delay(100);
  }
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("All Data Cleared");
  delay(2000);
  displayAttendanceScreen();
  
  Serial.println("🗑️  All data cleared");

  // FIX: Sync with Phone immediately
  if (deviceConnected) {
    sendStatistics();
    sendAllStudents();    // Clears student list on phone
    sendAttendanceLogs(); // Clears logs on phone
  }
}

void showStartupAnimation() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Smart Attendance");
  
  for (int i = 0; i < 16; i++) {
    lcd.setCursor(i, 1);
    lcd.write(0xFF);
    delay(30);
  }
  
  delay(500);
  lcd.clear();
  lcd.setCursor(2, 0);
  lcd.print("Initializing");
  
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++) {
      lcd.setCursor(6 + j, 1);
      lcd.print(".");
      delay(200);
    }
    lcd.setCursor(6, 1);
    lcd.print("   ");
    delay(100);
  }
}

// ==================== BLE FUNCTIONS ====================
void initBLE() {
  Serial.println("🔵 Initializing BLE...");
  
  BLEDevice::init("SmartAttendanceV3");
  
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  
// Wrap SERVICE_UUID in BLEUUID() to fix the ambiguity error
BLEService *pService = pServer->createService(BLEUUID(SERVICE_UUID), 30);
  
  pCharScan = pService->createCharacteristic(
    CHAR_SCAN_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  pCharScan->addDescriptor(new BLE2902());
  
pCharStudentRx = pService->createCharacteristic(
   CHAR_STUDENT_RX_UUID,
   // Add WRITE_NR so the phone can send data faster without waiting for a handshake
   BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
 ); 
 
  pCharStudentRx->setCallbacks(new StudentDataCallback());
  
  pCharStudentTx = pService->createCharacteristic(
    CHAR_STUDENT_TX_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  pCharStudentTx->addDescriptor(new BLE2902());
  
  pCharStats = pService->createCharacteristic(
    CHAR_STATS_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  pCharStats->addDescriptor(new BLE2902());
  
  pCharLogs = pService->createCharacteristic(
    CHAR_LOGS_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  pCharLogs->addDescriptor(new BLE2902());

pCharCommand = pService->createCharacteristic(
    CHAR_COMMAND_UUID,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY
);
pCharCommand->addDescriptor(new BLE2902());

pCharCommand->setCallbacks(new CommandCallback());

  pService->start();
  
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  BLEDevice::startAdvertising();
  
  Serial.println("✓ BLE advertising started!");
}

void handleBLEConnection() {
  static bool wasConnected = false;
  if (!deviceConnected && wasConnected) {
    delay(500);
    pServer->startAdvertising();
    wasConnected = false;
  }
  if (deviceConnected) wasConnected = true;
}

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  Serial.println("\n╔════════════════════════════════════╗");
  Serial.println("║  Smart Attendance System v2.0     ║");
  Serial.println("║  Offline Master Mode              ║");
  Serial.println("╚════════════════════════════════════╝\n");

uint8_t newMac[6] = {0x24, 0x6F, 0x28, 0x01, 0x04, 0x05}; 
 esp_base_mac_addr_set(newMac);
  // ============================================================

  initBLE();

  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(BTN_MODE, INPUT_PULLUP);
  pinMode(BTN_CLEAR, INPUT_PULLUP);

  lcd.init();
  lcd.backlight();
  lcd.createChar(0, checkMark);
  lcd.createChar(1, heart);
  lcd.createChar(2, wifi);
  lcd.createChar(3, personIcon);

  showStartupAnimation();

  SPI.begin();
  rfid.PCD_Init();
  delay(100);
  
  byte version = rfid.PCD_ReadRegister(rfid.VersionReg);
  Serial.print("MFRC522 Version: 0x");
  Serial.println(version, HEX);

  preferences.begin("attendance", false);
  
  totalStudents = preferences.getInt("totalStudents", 0);
  daySession = preferences.getInt("daySession", 0);
  currentDate = getCurrentDate();
  
  Serial.print("📊 Total Students: ");
  Serial.println(totalStudents);
  

  lcd.clear();
  displayAttendanceScreen();
  playReadyTone();
  
  Serial.println("✓ System Ready!\n");
}

// ==================== MAIN LOOP ====================
void loop() {
  handleBLEConnection();
  handleButtons();
  
  if (millis() - lastLCDUpdate > LCD_UPDATE_INTERVAL) {
    updateLCDAnimation();
    updateDateCheck();
    lastLCDUpdate = millis();
  }
  
  if (millis() - lastStatsUpdate > STATS_UPDATE_INTERVAL) {
    updateStatistics();
    lastStatsUpdate = millis();
  }
  
  if (millis() - lastCardScan > SCAN_DELAY) {
    if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
      String uid = getCardUID();
      handleCardScan(uid);
      lastCardScan = millis();
      
      rfid.PICC_HaltA();
      rfid.PCD_StopCrypto1();
    }
  }
}
