#include <Arduino.h>
#include <WiFi.h>
#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ===== LED de usuario =====
#define LED_PIN 25 

// ===== CONFIGURACIÓN OLED =====
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ===== CONFIGURACIÓN APRS =====
const char* callsign = "Ti0tec5-7";   
const char* passcode = "26556";       
const char* server   = "rotate.aprs2.net";
const int   port     = 14580;

// ===== CONFIGURACIÓN WIFI =====
const char* ssid     = "Ubnt1_Casa_4";
const char* password = "cartago4";

// ===== CONFIGURACIÓN LoRa =====
#define LORA_SCK     5
#define LORA_MISO    19
#define LORA_MOSI    27
#define LORA_CS      18
#define LORA_RST     14
#define LORA_IRQ     26
#define LORA_BAND    433.775E6

// ===== CONFIGURACIÓN BEACON =====
const float BEACON_LAT = 9.8599407;
const float BEACON_LON = -83.9063452;
const char* BEACON_COMMENT = "iGATE Brainer";
const unsigned long BEACON_INTERVAL = 180000; // 3 min
unsigned long lastBeaconTime = 0;

WiFiClient aprsClient;
unsigned long packetsLoRaRX = 0;
unsigned long packetsAPRSIS_RX = 0;
unsigned long packetsAPRSIS_TX = 0;

// ===== Función timestamp =====
String getTimestamp() {
  unsigned long seconds = millis() / 1000;
  unsigned long minutes = seconds / 60;
  seconds %= 60;
  minutes %= 60;
  char timestamp[12];
  sprintf(timestamp, "[%02lu:%02lu] ", minutes, seconds);
  return String(timestamp);
}

// ===== Conexión APRS-IS =====
bool connectToAPRSIS() {
  Serial.print(getTimestamp());
  Serial.print("Conectando a ");
  Serial.print(server);
  Serial.print(":");
  Serial.println(port);
  
  if (aprsClient.connect(server, port)) {
    Serial.println(getTimestamp() + "✓ Conectado a APRS-IS");
    delay(1000);
    while (aprsClient.available()) {
      String response = aprsClient.readStringUntil('\n');
      Serial.println(getTimestamp() + "SRV_INIT: " + response);
    }
    String auth = "user " + String(callsign) +
                  " pass " + String(passcode) +
                  " vers TTGO-LoRa-iGate 1.0 " +
                  "filter r/9.85/-83.90/200\n";
    aprsClient.print(auth);
    Serial.print(getTimestamp() + "AUTH_SEND: ");
    Serial.print(auth);

    return true;
  } else {
    Serial.println(getTimestamp() + "✗ Fallo conexión APRS-IS");
    return false;
  }
}

// ===== Beacon =====
void sendBeacon() {
  if (!aprsClient.connected()) return;
  
  int lat_deg = abs((int)BEACON_LAT);
  float lat_min = (abs(BEACON_LAT) - lat_deg) * 60.0;
  char lat_dir = BEACON_LAT >= 0 ? 'N' : 'S';
  
  int lon_deg = abs((int)BEACON_LON);
  float lon_min = (abs(BEACON_LON) - lon_deg) * 60.0;
  char lon_dir = BEACON_LON >= 0 ? 'E' : 'W';
  
  String beaconPacket = String(callsign);
  beaconPacket += ">APRS,TCPIP:=";
  
  char position[30];
  sprintf(position, "%02d%05.2f%c/%03d%05.2f%c", 
          lat_deg, lat_min, lat_dir,
          lon_deg, lon_min, lon_dir);
  
  beaconPacket += String(position);
  beaconPacket += "&";
  beaconPacket += BEACON_COMMENT;
  beaconPacket += "\n";
  
  aprsClient.print(beaconPacket);
  packetsAPRSIS_TX++; // contar TX
  lastBeaconTime = millis();

  Serial.println(getTimestamp() + "BEACON_TX: " + beaconPacket);
  Serial.println(getTimestamp() + "✓ Beacon enviado correctamente");
}

// ===== Procesar APRS entrante =====
void processAPRSTraffic() {
  static String buffer = "";
  while (aprsClient.available()) {
    char c = aprsClient.read();
    buffer += c;
    if (c == '\n') {
      buffer.trim();
      if (buffer.length() > 0 && buffer.charAt(0) != '#') {
        packetsAPRSIS_RX++;
        Serial.println(getTimestamp() + "SRV_SYS: " + buffer);
      }
      buffer = "";
    }
  }
}

// ===== Procesar LoRa entrante =====
void processLoRaPacket(int packetSize) {
  if (packetSize == 0) return;
  String packet = "";
  for (int i = 0; i < packetSize; i++) {
    packet += (char)LoRa.read();
  }
  packetsLoRaRX++;
  Serial.println(getTimestamp() + "LoRa RX: " + packet);
}

// ===== Mostrar estado en OLED =====
void updateOLED() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0,0);
  display.print("WiFi: ");
  display.println(WiFi.status() == WL_CONNECTED ? ssid : "Descon.");

  display.setCursor(0,10);
  display.print("APRS-IS: ");
  display.println(aprsClient.connected() ? server : "No conectado");

  display.setCursor(0,20);
  display.print("LoRa RX: ");
  display.println(packetsLoRaRX);

  display.setCursor(0,30);
  display.print("APRS RX: ");
  display.println(packetsAPRSIS_RX);

  display.setCursor(0,40);
  display.print("APRS TX: ");
  display.println(packetsAPRSIS_TX);

  display.display();
}

// ===== Setup =====
void setup() {
  Serial.begin(115200);
  delay(1000);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);  

  // OLED
  Wire.begin(21, 22);
  if(!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("❌ No se encontró OLED en dirección 0x3C");
    for(;;);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);
  display.println("Iniciando iGate...");
  display.display();

  // WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n" + getTimestamp() + "✓ WiFi conectado!");
  Serial.println(getTimestamp() + "IP: " + WiFi.localIP().toString());

  // LoRa
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  LoRa.setPins(LORA_CS, LORA_RST, LORA_IRQ);
  if (!LoRa.begin(LORA_BAND)) {
    Serial.println(getTimestamp() + "✗ Error iniciando LoRa!");
    return;
  }
  Serial.println(getTimestamp() + "✓ LoRa iniciado (SF12, BW125kHz)");

  // APRS-IS
  connectToAPRSIS();
  sendBeacon();
}

// ===== Loop =====
void loop() {
  int packetSize = LoRa.parsePacket();
  if (packetSize) processLoRaPacket(packetSize);
  if (aprsClient.connected()) {
    processAPRSTraffic();
    if (millis() - lastBeaconTime > BEACON_INTERVAL) {
      sendBeacon();
    }
  }
  updateOLED();
  delay(200);
}



