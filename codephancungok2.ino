#include <WiFi.h>
#include <PubSubClient.h>

// ---------- Cấu hình ----------
// WiFi
const char* ssid = "iPhone";
const char* password = "12345678";

// MQTT
const char* mqtt_server = "172.21.0.72";
const int mqtt_port = 1883;
const char* mqtt_user = "bong";
const char* mqtt_pass = "Bong@2625";

// Topic
const char* topic_mq2_value   = "gas/mq2/value";
const char* topic_mq2_alert   = "gas/mq2/alert";
const char* topic_mh_value    = "gas/mq135/value";
const char* topic_mh_alert    = "gas/mq135/alert";
const char* topic_relay2_control = "relay2/control";

// Chân kết nối
const int gasPin    = 32;
const int smokePin  = 33;
const int relayPin1 = 23; // Còi + đèn
const int relayPin2 = 22; // Bơm
const int buttonPin = 13; // Nút dừng khẩn cấp

// Ngưỡng
const int gas_threshold   = 1500;
const int smoke_threshold = 1500;

// ---------- Biến toàn cục ----------
WiFiClient espClient;
PubSubClient client(espClient);

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
      client.subscribe(topic_relay2_control);
    } else {
      // Serial.print("MQTT failed, rc=");
      // Serial.println(client.state());
    }
  }
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) message += (char)payload[i];

  if (String(topic) == topic_relay2_control) {
    relay2_remote_state = (message == "1");
    // Lưu ý: Việc thực thi bật/tắt relay sẽ nằm trong Loop để đảm bảo an toàn
  }
}

// ---------- Setup ----------
void setup() {
  // Serial.begin(115200); // Bật lên để debug nếu cần

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

  // 3. XỬ LÝ LOGIC ĐIỀU KHIỂN (Ưu tiên an toàn số 1)
  
  if (buttonState == HIGH) { 
    // --- TRƯỜNG HỢP KHẨN CẤP (Nút đang nhấn) ---
    digitalWrite(relayPin1, LOW); // Tắt hết
    digitalWrite(relayPin2, LOW); // Tắt hết
    // Serial.println("EMERGENCY STOP!"); 
    
  } else {
    // --- TRƯỜNG HỢP BÌNH THƯỜNG (Hoạt động tự động) ---
    
    // Logic Relay 1 (Cảm biến Gas/Khói)
    bool isDanger = (gasValue >= gas_threshold || smokeValue >= smoke_threshold);
    digitalWrite(relayPin1, isDanger ? HIGH : LOW);

    // Logic Relay 2 (Bơm - Điều khiển từ xa)
    // Chỉ cho phép bật bơm khi KHÔNG nhấn nút dừng khẩn cấp
    digitalWrite(relayPin2, relay2_remote_state ? HIGH : LOW);
  }

  // 4. GỬI DỮ LIỆU MQTT (Mỗi 1 giây)
  unsigned long now = millis();
  if (now - lastPublish >= publishInterval) {
    lastPublish = now;
    
    // Chỉ gửi khi có kết nối để tránh lỗi
    if (client.connected()) {
      client.publish(topic_mq2_value, String(gasValue).c_str());
      client.publish(topic_mh_value, String(smokeValue).c_str());
      client.publish(topic_mq2_alert, gasValue >= gas_threshold ? "1" : "0");
      client.publish(topic_mh_alert, smokeValue >= smoke_threshold ? "1" : "0");
    }
  }
}