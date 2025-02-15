#include <ESP8266WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <SoftwareSerial.h>
#include <Wire.h>
#include <RTClib.h> // Thêm thư viện RTC

// Thông tin WiFi
const char* ssid = "realme 6i";
const char* password = "12345678";

// WebSocket client
WebSocketsClient webSocket;
SoftwareSerial arduino(D1, D2); // Serial giao tiếp với Arduino

// Biến điều khiển
int pumpSpeed = 0;
bool pumpState = false;
bool autoMode = false;
int moistureThresholdLow = 30;
int moistureThresholdHigh = 70;
int moisture = 0;
int temperature = 0;

// Biến thời gian
RTC_DS3231 rtc; // Đối tượng RTC
unsigned long previousMillis = 0;
const long interval = 5000; // Đọc cảm biến mỗi 5 giây

// Biến lưu trữ thời gian từ WebSocket
String turnOnTime = "";
String turnOffTime = "";
bool useTimeControl = false;

void setup() {
  Serial.begin(9600);
  arduino.begin(9600);

  WiFi.begin(ssid, password);

  // Đợi kết nối WiFi
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected. IP address: " + WiFi.localIP().toString());

  // Kết nối WebSocket server
  webSocket.begin("192.168.100.231", 8081, "/"); // Đổi IP thành của server Node.js
  webSocket.onEvent(webSocketEvent);
  Serial.println("Connected to WebSocket server.");

  // Khởi tạo I2C với các chân D6 (SCL) và D5 (SDA)
  Wire.begin(D5, D6); // SDA = D5, SCL = D6
  // Khởi tạo RTC
  if (!rtc.begin()) {
    Serial.println("Couldn't find RTC");
    while (1);
  }
  rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
}

void loop() {
  webSocket.loop();
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    readSensors();
  }
}

void webSocketEvent(WStype_t type, uint8_t *payload, size_t length) {
  if (type == WStype_TEXT) {
    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (!error) {
      // Nhận giá trị từ WebSocket
      if (doc.containsKey("autoMode")) {
        autoMode = doc["autoMode"];
        Serial.println("AutoMode received: " + String(autoMode ? "true" : "false"));
      }

      if (doc.containsKey("pumpState")) {
        pumpState = doc["pumpState"];
        Serial.println(pumpState ? "Pump ON" : "Pump OFF");
      }

      if (doc.containsKey("pumpSpeed")) {
        pumpSpeed = doc["pumpSpeed"];
        Serial.println("Pump speed: " + String(pumpSpeed));
      }

      if (doc.containsKey("moistureThresholdLow")) {
        moistureThresholdLow = doc["moistureThresholdLow"];
        Serial.println("Moisture Threshold Low: " + String(moistureThresholdLow));
      }

      if (doc.containsKey("moistureThresholdHigh")) {
        moistureThresholdHigh = doc["moistureThresholdHigh"];
        Serial.println("Moisture Threshold High: " + String(moistureThresholdHigh));
      }

      if (doc.containsKey("useTimeControl")) {
        useTimeControl = doc["useTimeControl"];
        Serial.println("useTimeControl: " + String(useTimeControl ? "true" : "false"));
      }

      if (doc.containsKey("turnOnTime")) {
        turnOnTime = doc["turnOnTime"].as<String>(); // Sửa ở đây
        Serial.println("Turn On Time: " + turnOnTime);
      }

      if (doc.containsKey("turnOffTime")) {
        turnOffTime = doc["turnOffTime"].as<String>(); // Sửa ở đây
        Serial.println("Turn Off Time: " + turnOffTime);
      }
      
    }
    // Gửi điều khiển bơm nếu không ở chế độ tự động
    if (!autoMode) {
      sendPumpControl();
    }
  }
}

void readSensors() {
  StaticJsonDocument<200> doc;
  doc["request"] = "read_sensors";
  String jsonString;
  serializeJson(doc, jsonString);
  arduino.println(jsonString); // Gửi yêu cầu đọc dữ liệu tới Arduino
  delay(1000);

  if (arduino.available() > 0) {//nhạn dữ lijeu phản hồi
    String response = arduino.readStringUntil('\n');
    response.trim();

    StaticJsonDocument<200> responseDoc;
    DeserializationError error = deserializeJson(responseDoc, response);

    if (!error) {
      moisture = responseDoc["moisture"];
      temperature = responseDoc["temperature"];
      StaticJsonDocument<200> sensorData;
      sensorData["moisture"] = moisture;
      sensorData["temperature"] = temperature;
      String jsonSensorData;
      serializeJson(sensorData, jsonSensorData);
      webSocket.sendTXT(jsonSensorData); // Gửi dữ liệu cảm biến tới server

      // Xử lý chế độ tự động
      if (autoMode) {
        // Kiểm tra thời gian thực
        DateTime now = rtc.now();
        String currentTime = String(now.hour()) + ":" + String(now.minute());

        Serial.println("Current Time: " + currentTime);
        
        // So sánh thời gian với thời gian gửi từ web
        if (useTimeControl) {
          if (currentTime >= turnOnTime && currentTime <= turnOffTime) {
            // Nếu thời gian thực nằm trong khoảng thời gian từ turnOnTime đến turnOffTime
            pumpState = false; // Không bật máy bơm
            Serial.println("Pump OFF due to time control.");
          } else {
            // Nếu không trong khoảng thời gian, kiểm tra độ ẩm
            if (moisture < moistureThresholdLow) {
              pumpState = true; // Bật máy bơm nếu độ ẩm thấp
            } else if (moisture > moistureThresholdHigh) {
              pumpState = false; // Tắt máy bơm nếu độ ẩm cao
            }
          }
        } else {
          if (moisture < moistureThresholdLow) {
            pumpState = true; // Bật máy bơm nếu độ ẩm thấp
          } else if (moisture > moistureThresholdHigh) {
            pumpState = false; // Tắt máy bơm nếu độ ẩm cao
          }
        }
        sendPumpControl();
      }
    } else {
      Serial.println("Failed to parse JSON from Arduino.");
    }
  }
}

void sendPumpControl() {
  StaticJsonDocument<200> doc;
  doc["request"] = "control_pump";
  doc["state"] = pumpState ? "on" : "off";
  doc["speed"] = pumpSpeed;
  doc["mode"] = autoMode ? "auto" : "manual";

  String jsonString;
  serializeJson(doc, jsonString);
  arduino.println(jsonString); // Gửi điều khiển bơm tới Arduino để thực hiện
  Serial.println("Pump control JSON sent to Arduino: " + jsonString);

  if (webSocket.isConnected()) {
    webSocket.sendTXT(jsonString); // Gửi điều khiển bơm tới WebSocket server để lưu vào server
    Serial.println("Pump control JSON sent to server: " + jsonString);
  } else {
    Serial.println("WebSocket not connected. Unable to send data to server.");
  }
}
