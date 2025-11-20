#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>  
#include <ESP32Servo.h>

// ---------- Cấu hình ----------
// WiFi
const char* ssid = "Không cho";
const char* password = "27052705";

// MQTT
 const char* mqtt_server = "172.20.10.7";
 const int mqtt_port = 1883;
 const char* mqtt_user = "";
 const char* mqtt_pass = "";
// MQTT TOPICS - CHUẨN HÓA THEO NODE-RED
// ====================================
// ESP32 GỬI LÊN (Publish)
const char* TOPIC_SENSORS = "pccc/esp32/sensors";   // Gửi JSON: {smoke, gas, relay2, timestamp}
const char* TOPIC_STATUS = "pccc/esp32/status";     // Gửi heartbeat

// ESP32 NHẬN VỀ (Subscribe)
const char* TOPIC_SERVO = "pccc/esp32/servo";       // Nhận: {servo_pan, servo_tilt, action}
const char* TOPIC_RELAY = "pccc/esp32/relay";       // Nhận: {relay2, duration}


// Chân kết nối
const int gasPin    = 32;
const int smokePin  = 33;
const int relayPin1 = 23; // Còi + đèn
const int relayPin2 = 22; // Bơm
const int buttonPin = 13; // Nút dừng khẩn cấp
const int PIN_SERVO_PAN  = 21; // pan khoogn tilt

// Ngưỡng
const int gas_threshold   = 1500;
const int smoke_threshold = 1500;

// ---------- Biến toàn cục ----------
WiFiClient espClient;
PubSubClient client(espClient);
Servo servoPan;
int servoPanAngle = 90;

// Timer biến (dùng millis thay cho delay)
unsigned long lastPublish = 0;
unsigned long lastReconnectAttempt = 0;
const unsigned long publishInterval = 1000; // 1 giây gửi 1 lần
const unsigned long reconnectInterval = 5000; // 5 giây thử kết nối lại 1 lần

bool relay2_remote_state = false; // Lưu trạng thái lệnh từ server

// ---------- Functions ----------

void setup_wifi() {
  if (WiFi.status() == WL_CONNECTED) return;

  // Chỉ bắt đầu kết nối, không dùng while chờ đợi để tránh treo code
  WiFi.begin(ssid, password);
  // Serial.println("Dang ket noi Wifi...");
}

void reconnect_mqtt() {
  // Nếu đã kết nối rồi thì thoát ngay
  if (client.connected()) return;

  // Chỉ thử kết nối lại mỗi 5 giây (Non-blocking)
  unsigned long now = millis();
  if (now - lastReconnectAttempt > reconnectInterval) {
    lastReconnectAttempt = now;
    
    // Tạo ID ngẫu nhiên để tránh xung đột với ESP cũ
    String clientId = "ESP32_Client_";
    clientId += String(random(0xffff), HEX);

    if (client.connect(clientId.c_str(), mqtt_user, mqtt_pass)) {
      // Serial.println("MQTT Connected!");
      client.subscribe(TOPIC_RELAY);
      client.subscribe(TOPIC_SERVO);
    } else {
      // Serial.print("MQTT failed, rc=");
      // Serial.println(client.state());
    }
  }
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  // Chuyển payload thành string
  String message;
  for (unsigned int i = 0; i < length; i++) message += (char)payload[i];

  // Tạo topicStr để so sánh
  String topicStr = String(topic);

  // 
  // --------- XỬ LÝ LỆNH RELAY2 TỪ SERVER ---------
if (topicStr == TOPIC_RELAY) {

    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, payload, length);

    String cmd = "";

    if (!error && doc.containsKey("relay2")) {
        // Trường hợp JSON: {"relay2": "fire"}
        cmd = String(doc["relay2"].as<const char*>());
    } 
    else {
        // Trường hợp gửi chuỗi raw: fire
        for (unsigned int i = 0; i < length; i++)
            cmd += (char)payload[i];
    }

    cmd.trim();
    cmd.toLowerCase();

    // -------------------------
    // 🔥 Chỉ hỗ trợ fire / safe
    // -------------------------
    if (cmd == "fire") {
        relay2_remote_state = true;
    } 
    else if (cmd == "safe") {
        relay2_remote_state = false;
    }

    Serial.print("[MQTT][RELAY] Command = ");
    Serial.println(cmd);

    Serial.print("[MQTT][RELAY] relay2_remote_state = ");
    Serial.println(relay2_remote_state ? "ON" : "OFF");

    return;
}


  // --- Xử lý Servo ---
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, payload, length);
  if (!error && topicStr == TOPIC_SERVO) {
    if (doc.containsKey("servo_pan")) {
      servoPanAngle = doc["servo_pan"];
      servoPan.write(servoPanAngle);
      Serial.println("[SERVO] Pan: " + String(servoPanAngle) + "°");
    }

    String action = doc["action"] | "move";
    Serial.println("[SERVO] Action: " + action);
  }
}


// ---------- Setup ----------
void setup() {
  // Serial.begin(115200); // Bật lên để debug nếu cần
  servoPan.attach(PIN_SERVO_PAN);
  servoPan.write(90);  // vị trí ban đầu, giữa

  pinMode(relayPin1, OUTPUT);
  pinMode(relayPin2, OUTPUT);
  
  // QUAN TRỌNG: Nút nhấn kích mức HIGH -> Dùng điện trở kéo xuống (PULLDOWN)
  // Trạng thái bình thường (không nhấn) = LOW. Nhấn = HIGH (3.3V)
  pinMode(buttonPin, INPUT_PULLDOWN); 

  // Trạng thái ban đầu: TẮT HẾT
  digitalWrite(relayPin1, LOW);
  digitalWrite(relayPin2, LOW);
  
  analogReadResolution(12);

  setup_wifi(); // Gọi lần đầu
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqtt_callback);
}

// ---------- Loop ----------
void loop() {
  // 1. QUẢN LÝ KẾT NỐI (Không chặn code chính)
  if (WiFi.status() != WL_CONNECTED) {
      // Nếu mất Wifi, thử kết nối lại (nhưng không dừng chương trình)
      static unsigned long lastWifiCheck = 0;
      if (millis() - lastWifiCheck > 5000) {
         lastWifiCheck = millis();
         WiFi.reconnect();
      }
  } else {
      // Có Wifi thì lo vụ MQTT
      reconnect_mqtt();
      client.loop();
  }

  // 2. ĐỌC CẢM BIẾN & NÚT NHẤN
  int gasValue = analogRead(gasPin);
  int smokeValue = analogRead(smokePin);
  int buttonState = digitalRead(buttonPin);


  // BẬT TẮT RELAY 2//
  if (relay2_remote_state) {
    digitalWrite(relayPin2, HIGH);
  } else {
    digitalWrite(relayPin2, LOW);
  }


  // 3. XỬ LÝ LOGIC ĐIỀU KHIỂN (Ưu tiên an toàn số 1)
  
  if (buttonState == HIGH) { 
    // --- TRƯỜNG HỢP KHẨN CẤP (Nút đang nhấn) ---
    digitalWrite(relayPin1, LOW); // Tắt hết
  
    // Serial.println("EMERGENCY STOP!"); 
    
  } else {
    // --- TRƯỜNG HỢP BÌNH THƯỜNG (Hoạt động tự động) ---
    
    // Logic Relay 1 (Cảm biến Gas/Khói)
    bool isDanger = (gasValue >= gas_threshold || smokeValue >= smoke_threshold);
    digitalWrite(relayPin1, isDanger ? HIGH : LOW);
   
  }

  // // 4. GỬI DỮ LIỆU MQTT (Mỗi 1 giây)
  // unsigned long now = millis();
  // if (now - lastPublish >= publishInterval) {
  //   lastPublish = now;
    
  //   // Chỉ gửi khi có kết nối để tránh lỗi
  //   if (client.connected()) {
  //     client.publish(TOPIC_SENSORS, String(gasValue).c_str());
  //     client.publish(topic_mh_value, String(smokeValue).c_str());
      
  //   }
  // }
  // 4. GỬI DỮ LIỆU MQTT (Mỗi 1 giây)
unsigned long now = millis();
if (now - lastPublish >= publishInterval) {
    lastPublish = now;

    // Chỉ gửi khi có kết nối
    if (client.connected()) {
        // Tạo JSON gộp tất cả dữ liệu
        StaticJsonDocument<128> doc;
        doc["gas"] = gasValue;
        doc["smoke"] = smokeValue;
        doc["relay2"] = digitalRead(relayPin2);
        doc["timestamp"] = now;

        char buffer[128];
        serializeJson(doc, buffer);

        client.publish(TOPIC_SENSORS, buffer);

        Serial.println("[MQTT] → " + String(buffer)); // Debug
    }
}

}