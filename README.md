# Smart Attendance System - ESP32 (Offline Master Mode)

A fully offline-capable RFID attendance system built on the ESP32 platform. This system stores student data and attendance logs locally using Non-Volatile Storage (NVS) and syncs with a mobile app via Bluetooth Low Energy (BLE) for management and data retrieval.

## 🚀 Features

* **Offline First Architecture**: Works completely independently without Wi-Fi or constant app connectivity.
* **RFID Scanning**: Instant attendance marking using MFRC522 modules.
* **Local Storage**: Stores student names, UIDs, and classes directly on the ESP32.
* **Bluetooth (BLE) Connectivity**:
    * Sync student data (Add/Update/Delete).
    * Live scanning (Real-time updates to app).
    * Fetch historical logs and statistics.
* **Interactive UI**: 16x2 I2C LCD displaying student names, daily counts, and system status.
* **Audio Feedback**: Distinct buzzer tones for success, error, ready, and enrollment.
* **Physical Controls**: Buttons to cycle modes (Attendance/Stats/View) and clear daily data.

## 🛠️ Hardware Requirements

* **ESP32 Development Board**
* **MFRC522 RFID Reader**
* **16x2 LCD Display** (with I2C Backpack)
* **Push Buttons** (x2)
* **Active Buzzer**
* **LED** (Status Indicator)
* Jumper Wires & Breadboard/PCB

## 🔌 Pin Configuration

Based on the code definitions:

| Component | Pin Name | ESP32 Pin |
| :--- | :--- | :--- |
| **MFRC522** | SDA (SS) | GPIO 5 |
| | RST | GPIO 4 |
| | MOSI | GPIO 23 (Default VSPI) |
| | MISO | GPIO 19 (Default VSPI) |
| | SCK | GPIO 18 (Default VSPI) |
| **LCD (I2C)** | SDA | GPIO 21 |
| | SCL | GPIO 22 |
| **Controls** | Mode Button | GPIO 33 |
| | Clear Button | GPIO 32 |
| **Outputs** | Buzzer | GPIO 25 |
| | LED | GPIO 2 |

## 📦 Software & Libraries

This project uses the Arduino IDE. You will need to install the following libraries via the Library Manager:

1.  **MFRC522** (by GithubCommunity)
2.  **LiquidCrystal_I2C** (by Frank de Brabander)
3.  **ArduinoJson** (Version 6.x or higher)
4.  **ESP32 BLE Arduino** (Built-in with ESP32 board manager)

## ⚙️ Installation

1.  **Setup Arduino IDE**: Ensure you have the ESP32 board support installed.
2.  **Install Libraries**: Install the required libraries listed above.
3.  **Config**: Check `smart_attendance.ino` defines if you need to change any pin numbers.
4.  **Upload**: Select your ESP32 board and port, then upload the sketch.
    * *Note: If you encounter partition scheme errors, select "Huge App" partition scheme in Tools.*

## 📱 BLE Interface Specification

For developers building the companion mobile app, the system exposes the following BLE Service and Characteristics:

**Service UUID**: `4fafc201-1fb5-459e-8fcc-c5c9c331914b`

| Characteristic | UUID | Properties | Function |
| :--- | :--- | :--- | :--- |
| **Scan Data** | `...26a8` | READ, NOTIFY | Sends real-time scanned UID or attendance JSON. |
| **Student RX** | `...e8cd` | WRITE | App sends JSON to Add/Edit/Delete students. |
| **Student TX** | `...021b` | NOTIFY | ESP32 sends the full list of students to the app. |
| **Stats** | `...da91` | NOTIFY | Sends total students, present count, and date. |
| **Logs** | `...5f2d` | NOTIFY | Sends historical attendance logs. |
| **Command** | `...9e0f` | WRITE | Send commands like `GET_STUDENTS`, `CLEAR_ALL`. |

## 📖 Usage Guide

### 1. Attendance Mode (Default)
* The screen shows "Ready to Scan" with Present (P), Absent (A), and Total (T) counts.
* Scan a registered card to mark attendance.
* Scan an unregistered card to see "Unknown Card" (or send UID to app for registration if connected).

### 2. Button Functions
* **Mode Button (GPIO 33)**: Single press cycles between:
    1.  **Attendance Screen**: Standard scanning interface.
    2.  **Stats Screen**: Shows the name of the last person scanned.
    3.  **View Students**: Shows total student count.
* **Clear Button (GPIO 32)**:
    * **Long Press (3s)**: Clears **only today's** attendance records (resets daily counter).

### 3. Factory Reset
* To wipe ALL data (Students + Logs), send the command `CLEAR_ALL` via the mobile app BLE terminal.

## 🤝 Contributing
Athishay Jain
