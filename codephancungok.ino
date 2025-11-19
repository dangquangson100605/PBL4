#include <WiFi.h>
#include <PubSubClient.h>

// ---------- MQTT & Wi-Fi ----------
const char* topic_mq2_value   = "gas/mq2/value"; // gửi
const char* topic_mq2_alert   = "gas/mq2/alert"; // cảnh báo
const char* topic_mh_value    = "gas/mq135/value"; // gửi
const char* topic_mh_alert    = "gas/mq135/alert"; // cảnh báo
const char* topic_relay2_control = "relay2/control"; // nhận lệnh từ server

// Thông tin Wi-Fi
const char* ssid = "iPhone";
const char* password = "12345678";

// Thông tin MQTT broker
const char* mqtt_server = "172.21.0.72";
const int mqtt_port = 1883;
const char* mqtt_user = "bong";
const char* mqtt_pass = "Bong@2625";

WiFiClient espClient;
PubSubClient client(espClient);

// ---------- Biến trạng thái ----------
bool relay1_state = false; // trạng thái relay1
bool relay2_state = false; // trạng thái relay2

// ---------- Chân kết nối ----------
const int gasPin    = 32; // đọc cảm biến gas
const int smokePin  = 33; // đọc cảm biến khói
const int relayPin1 = 23; // relay1 (còi + đèn)
const int relayPin2 = 22; // relay2 (bơm)
const int buttonPin = 13; // nút nhấn

// ---------- Ngưỡng ----------
const int gas_threshold   = 1000;
const int smoke_threshold = 1000;

// ---------- Biến đọc sensor ----------
int gasValue = 0;
int smokeValue = 0;
int buttonState = 0;

// ---------- Timer gửi MQTT ----------
unsigned long lastPublish = 0;
const unsigned long publishInterval = 1000; // 1 giây

// ---------- Functions ----------
void setup_wifi() {
  WiFi.begin(ssid, password);
  unsigned long startAttemptTime = millis();

  // Thử kết nối tối đa 10 giây
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
    delay(500);
    // Serial.print("."); // debug Wi-Fi
  }

  // Nếu muốn debug Wi-Fi, có thể bật các Serial.println dưới đây
  /*
  if (WiFi.status() == WL_CONNECTED) Serial.println("Wi-Fi OK");
  else Serial.println("KHONG KET NOI duoc, chay che do offline");
  */
}

void reconnect_mqtt() {
  while (!client.connected() && WiFi.status() == WL_CONNECTED) {
    if (client.connect("ESP32Client", mqtt_user, mqtt_pass)) {
      client.subscribe(topic_relay2_control); // đăng ký nhận lệnh relay2
    } else {
      delay(3000);
      // Serial.println("MQTT reconnect failed"); // debug
    }
  }
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) message += (char)payload[i];

  if (String(topic) == topic_relay2_control) {
    relay2_state = (message == "1");
    digitalWrite(relayPin2, relay2_state ? HIGH : LOW);
    // Serial.print("Relay2 state received: "); Serial.println(message); // debug
  }
}

// ---------- Setup ----------
void setup() {
  // Serial.begin(9600); // chỉ dùng khi debug

  pinMode(relayPin1, OUTPUT);
  pinMode(relayPin2, OUTPUT);
  
  pinMode(buttonPin, INPUT_PULLDOWN);


  digitalWrite(relayPin1, LOW);
digitalWrite(relayPin2, LOW);
  analogReadResolution(12);

  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqtt_callback);
}

// ---------- Loop ----------
void loop() {
  // Kết nối lại Wi-Fi & MQTT nếu mất
  if (WiFi.status() != WL_CONNECTED) setup_wifi();
  if (!client.connected()) reconnect_mqtt();
  client.loop();

  // 1. Đọc sensor & nút nhấn
  gasValue = analogRead(gasPin);
  smokeValue = analogRead(smokePin);
  buttonState = digitalRead(buttonPin);

  // 2. Xử lý logic relay1 (ưu tiên nút nhấn)
  if (buttonState == HIGH) { 
    relay1_state = false;
    digitalWrite(relayPin1, LOW);
    // Serial.println("-> Che do: NGAT KHAN CAP (Do nhan nut)"); // debug
  } else {
    relay1_state = (gasValue >= gas_threshold || smokeValue >= smoke_threshold);
    digitalWrite(relayPin1, relay1_state ? HIGH : LOW);
    // Serial.println("-> Relay1 state auto updated"); // debug
  }

  // 3. Gửi dữ liệu & cảnh báo MQTT mỗi 1 giây
  unsigned long now = millis();
  if (now - lastPublish >= publishInterval && WiFi.status() == WL_CONNECTED && client.connected()) {
    lastPublish = now;
    client.publish(topic_mq2_value, String(gasValue).c_str());
    client.publish(topic_mh_value, String(smokeValue).c_str());
    client.publish(topic_mq2_alert, gasValue >= gas_threshold ? "1" : "0");
    client.publish(topic_mh_alert, smokeValue >= smoke_threshold ? "1" : "0");
    // Serial.println("-> MQTT data published"); // debug
  }
}
