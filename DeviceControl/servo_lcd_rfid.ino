#include <WiFi.h>
#include <ESP32Servo.h>
#include <LiquidCrystal_I2C.h>
#include <AsyncTCP.h>
#include <SPI.h>
#include <String.h>
#include <MFRC522.h>
#include <WebSocketsServer.h>
#include <WebServer.h>

// Thông tin Wi-Fi
const char* ssid = "";
const char* password = "";

// **Cài đặt cho TCP Server (Servo/LCD)**
const char* tcpHost = "";
const uint16_t tcpPort = 8584;
AsyncClient *tcpClient;

// **Cài đặt cho LCD**
LiquidCrystal_I2C lcd(0x27, 16, 2);

// **Cài đặt cho Servo**
Servo servo;
int servoPin = 15;
int servoAngle = 0;
bool servoOn = false;
unsigned long openTime = 0;

// **Cài đặt cho Server 
WiFiServer webServer(80);
WebServer apiServer(81);

// **Cài đặt cho RFID**
#define RFID_SS_PIN 5
#define RFID_RST_PIN 22
MFRC522 rfid(RFID_SS_PIN, RFID_RST_PIN);

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length);
void handleDisplayPost();
void displayOnLcd(String message);

// **Cài đặt cho WebSocket Server (RFID)**
const uint16_t webSocketPort = 8888;
WebSocketsServer webSocketServer(webSocketPort);

// **Hàm xử lý sự kiện WebSocket (RFID)**
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
    switch (type) {
        case WStype_DISCONNECTED:
            Serial.printf("[%u] Disconnected!\n", num);
            break;
        case WStype_CONNECTED: {
            Serial.printf("[%u] Connected! url: %s\n", num, payload);
        }
            break;
        case WStype_TEXT:
            Serial.printf("[%u] get Text: %s\n", num, payload);
            break;
        case WStype_BIN:
            Serial.printf("[%u] get binary length: %u\n", num, length);
            break;
        case WStype_PONG:
            Serial.printf("[%u] Pong!\n", num);
            break;
    }
}

void setup() {
    Serial.begin(115200);
    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.println("Ket noi toi WiFi...");
    }

    Serial.println("Da ket noi toi WiFi");
    Serial.print("Dia chi IP cua ESP32: ");
    Serial.println(WiFi.localIP());

    // **Khởi tạo Servo và LCD**
    servo.attach(servoPin);
    servo.write(servoAngle);
    lcd.init();
    lcd.backlight();
    lcd.setCursor(0, 0);
    lcd.print("   ATTENDANCE");
    lcd.setCursor(0, 1);
    lcd.print("     SYSTEM");
    delay(1000);

    // **Khởi tạo Web Server (Điều khiển Servo)**
    webServer.begin();
    Serial.println("Web server for Servo control started on port 80.");

    apiServer.on("/display", HTTP_POST, handleDisplayPost); // <--- ĐĂNG KÝ ĐIỂM CUỐI MỚI
    apiServer.onNotFound([](){ // Xử lý các tuyến đường không xác định cho apiServer
        apiServer.send(404, "application/json", "{\"status\": \"error\", \"message\": \"Điểm cuối API không tìm thấy.\"}");
    });
    apiServer.begin(); 

    // **Khởi tạo RFID**
    SPI.begin();
    rfid.PCD_Init();
    Serial.println("\nRFID Reader Initialized.");

    // **Khởi tạo WebSocket Server (RFID)**
    webSocketServer.begin();
    webSocketServer.onEvent(webSocketEvent);
    Serial.printf("WebSocket server for RFID started on ws://%s:%u\n", WiFi.localIP().toString().c_str(), webSocketPort);
}

void loop() {
    // **Xử lý Web Server (Điều khiển Servo)**
    WiFiClient webClient = webServer.available();
    if (webClient) {
        String request = webClient.readStringUntil('\r');
        Serial.println("[HTTP] " + request);

        if (request.indexOf("/on") != -1) {
            servoAngle = 90;
            servo.write(servoAngle);
            servoOn = true;
            openTime = millis();
        }
        if (request.indexOf("/off") != -1) {
            servoAngle = 0;
            servo.write(servoAngle);
            servoOn = false;
            openTime = 0;
        }

        webClient.println("HTTP/1.1 200 OK");
        webClient.println("Content-Type: text/html");
        webClient.println();
        webClient.println("<html><body>");
        webClient.println("<h1>Dieu Khien Thiet Bi</h1>");
        webClient.println("<p><a href='/on'><button>Bat Servo</button></a></p>");
        webClient.println("<p><a href='/off'><button>Tat Servo</button></a></p>");

        if (servoOn) {
            webClient.println("<p>Servo: Da bat</p>");
        } else {
            webClient.println("<p>Servo: Da tat</p>");
        }

        webClient.println("</body></html>");
        webClient.stop();
    }

    if (servoOn && (millis() - openTime >= 5000)) {
    servoAngle = 0;
    servo.write(servoAngle);
    servoOn = false;
    Serial.println("Servo tự động tắt sau 5 giây.");
    openTime = 0;
  }
  
  apiServer.handleClient();
    // **Xử lý RFID và WebSocket Server**
  webSocketServer.loop();
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
        Serial.print("[RFID] Card detected, UID: ");
        String cardId = "";
        for (byte i = 0; i < rfid.uid.size; i++) {
            cardId += (rfid.uid.uidByte[i] < 0x10 ? "0" : "") + String(rfid.uid.uidByte[i], HEX);
        }
        Serial.println(cardId);

        // Gửi ID thẻ RFID đến tất cả các client WebSocket đã kết nối
        webSocketServer.broadcastTXT(cardId.c_str(), cardId.length());

        rfid.PICC_HaltA();
        rfid.PCD_StopCrypto1();
        delay(1000);
  }
}

void handleDisplayPost() {
    Serial.println("handleDisplayPost() duoc goi!");
    String receivedText = "";
    if (apiServer.hasArg("plain")) { 
        receivedText = apiServer.arg("plain");
        Serial.print("Du lieu nhan duoc: "); 
        Serial.println(receivedText);
    }

    displayOnLcd(String(receivedText));
    delay(5000);
    lcd.clear(); 
    lcd.setCursor(0, 0);
    lcd.print("   ATTENDANCE");
    lcd.setCursor(0, 1);
    lcd.print("     SYSTEM");

    apiServer.send(200, "application/json", "{\"status\": \"success\", \"message\": \"Đã nhận và hiển thị dữ liệu.\", \"received_text\": \"" + receivedText + "\"}");
}

void displayOnLcd(String message) {
    message.trim();

    // Split the string into words
    String words[100]; // Assuming max 100 words
    int wordCount = 0;
    int startIdx = 0;

    for (int i = 0; i < message.length(); i++) {
        if (message.charAt(i) == ' ' || i == message.length() - 1) {
            if (i == message.length() - 1) i++;
            words[wordCount++] = message.substring(startIdx, i);
            startIdx = i + 1;
        }
    }

    // Display words on LCD
    lcd.clear();
    lcd.setCursor(0, 0);
    String displayLine1 = "";
    // Display all words except the last 3 on the first line
    for (int i = 0; i < wordCount - 3; i++) {
        displayLine1 += words[i] + " ";
    }
    lcd.print(displayLine1);

    // Display the last 3 words on the third line (index 2)
    lcd.setCursor(0, 1);
    String displayLine2 = "";
    for (int i = wordCount - 3; i < wordCount; i++) {
        displayLine2 += words[i] + " ";
    }
    lcd.print(displayLine2);
}

