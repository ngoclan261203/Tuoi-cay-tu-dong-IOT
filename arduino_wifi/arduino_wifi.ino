#include <SoftwareSerial.h>
#include <DHT.h>
#include <ArduinoJson.h>

#define DHTPIN A2              // Chân kết nối cảm biến DHT (chân A2)
#define DHTTYPE DHT11          // Loại cảm biến DHT là DHT11
#define SOIL_SENSOR_PIN A0     // Chân kết nối cảm biến độ ẩm đất (chân A0)
//có thể sử dụng bất kỳ chân analog nào (A0, A1, A2, A3, A4, hoặc A5, tùy thuộc vào loại bo Arduino).
//Cảm biến độ ẩm đất thường có đầu ra là tín hiệu analog (dòng điện hoặc điện áp), phản ánh độ ẩm của đất.
//ín hiệu này cần được đọc qua bộ chuyển đổi Analog-to-Digital Converter (ADC) của vi điều khiển, và trên Arduino, các chân A0 → A5 đều được nối với ADC
#define PUMP_ENA 9             // Chân điều khiển tốc độ máy bơm (PWM)
//vì nó là chân hỗ trợ PWM tạo tín hiệu xung để điều chỉnh dòng điện cung cấp đến động cơ, từ đó thay đổi tốc độ quay của máy bơm.
//Chỉ các chân PWM trên Arduino mới hỗ trợ điều chỉnh tín hiệu xung.
//Arduino Uno có các chân PWM: 3, 5, 6, 9, 10, 11 
#define PUMP_IN1 6             // Chân điều khiển chiều quay của máy bơm (chân 7)
#define PUMP_IN2 7             // Chân điều khiển chiều quay của máy bơm (chân 8)

// Cấu hình SoftwareSerial
SoftwareSerial softSerial(5, 3); // RX = 5 nối với d2, TX = 3 nói với d1





// Khởi tạo đối tượng điều khiển cảm biến DHT
DHT dht(DHTPIN, DHTTYPE);

void setup() {
  // Khởi tạo kết nối
  Serial.begin(9600);          // Cổng USB chính để debug
  softSerial.begin(9600);      // Kết nối với NodeMCU qua SoftwareSerial
  dht.begin();                 // Khởi động cảm biến DHT

  // Thiết lập chân đầu vào và đầu ra
  pinMode(SOIL_SENSOR_PIN, INPUT);
  pinMode(PUMP_ENA, OUTPUT);
  pinMode(PUMP_IN1, OUTPUT);
  pinMode(PUMP_IN2, OUTPUT);
  //high-low động cơ quay xuôi Máy bơm đẩy nước ra khỏi bể hoặc nguồn cung cấp ,
  //low-high dộng cơ quay ngược Máy bơm hút nước ngược trở lại từ đường dẫn vào nguồn (bể hoặc đường ống).
  //high -high là tt k hợp lệ và nguy hiểm, vì nó có thể dẫn đến hiện tượng ngắn mạch 
  //Điều này dẫn đến quá nhiệt, làm hỏng linh kiện vì có đòng điện quá lớn chạy qua
  digitalWrite(PUMP_IN1, LOW); // Tắt máy bơm ban đầu
  digitalWrite(PUMP_IN2, LOW);
  analogWrite(PUMP_ENA, 0);    // Máy bơm không quay
   //Điều chỉnh giá trị PWM để kiểm soát tốc độ bơm nước.
  //Tín hiệu PWM có chu kỳ xung hoàn toàn tắt (dòng điện = 0), dẫn đến máy bơm không nhận được năng lượng và sẽ không quay.
}


void loop() {
  // Kiểm tra xem có dữ liệu nhận được qua SoftwareSerial
  if (softSerial.available() > 0) {
    String request = softSerial.readStringUntil('\n');
    request.trim();
   
    // Tạo đối tượng JSON để xử lý yêu cầu
    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, request);
    if (error) {
      Serial.println("Invalid JSON format");
      return;
    }

    // Kiểm tra loại yêu cầu
    String reqType = doc["request"];
    Serial.println(reqType);
    if (reqType == "read_sensors") {
      sendSensorData();  // Đọc cảm biến và gửi dữ liệu
    } else if (reqType == "control_pump") {
      controlPump(doc);  // Điều khiển máy bơm
    }
  }
}


// Hàm đọc dữ liệu cảm biến và gửi dưới dạng JSON
void sendSensorData() {
  // Đọc độ ẩm đất
  int soilMoisture = analogRead(SOIL_SENSOR_PIN);//ọc giá trị tín hiệu tương tự (analog) từ chân được chỉ định
  //Giá trị trả về là một số nguyên từ 0 đến 1023
  soilMoisture = map(soilMoisture, 1023, 0, 0, 100); // Quy đổi độ ẩm đất về %
//fromLow = 1023: Điện áp ngõ vào cao nhất (đất khô hoàn toàn).
  //fromHigh = 0: Điện áp ngõ vào thấp nhất (đất ẩm ướt nhất).
  //toLow = 0: Độ ẩm đất tối thiểu (0%).
  //toHigh = 100: Độ ẩm đất tối đa (100%).

  // Đọc nhiệt độ từ cảm biến DHT
  float temperature = dht.readTemperature();

  // Tạo đối tượng JSON để lưu thông tin độ ẩm và nhiệt độ
  StaticJsonDocument<200> responseDoc;
  responseDoc["moisture"] = soilMoisture;
  responseDoc["temperature"] = temperature;

  // Chuyển đối tượng JSON thành chuỗi và gửi tới NodeMCU
  String response;
  serializeJson(responseDoc, response);
  softSerial.print(response);  // Gửi qua SoftwareSerial
  Serial.println(response);
}


// Hàm điều khiển máy bơm theo JSON từ NodeMCU
void controlPump(const JsonDocument& doc) {
  String pumpState = doc["state"];
  Serial.println(pumpState);
  
  int speed = doc["speed"];  // Giá trị tốc độ từ 0-255
  if (pumpState == "on") {
    digitalWrite(PUMP_IN1, HIGH);  // Bật máy bơm
    digitalWrite(PUMP_IN2, LOW);
    analogWrite(PUMP_ENA, speed);  // Điều chỉnh tốc độ máy bơm
  } else {
    digitalWrite(PUMP_IN1, LOW);   // Tắt máy bơm
    digitalWrite(PUMP_IN2, LOW);
    analogWrite(PUMP_ENA, 0);
  }
}
