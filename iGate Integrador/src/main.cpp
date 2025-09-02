#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <SPI.h>
#include <LoRa.h>

// ===== Pines LilyGO LoRa32 v1.6.1 =====
#define LORA_SCK   5
#define LORA_MISO  19
#define LORA_MOSI  27
#define LORA_CS    18
#define LORA_RST   14
#define LORA_IRQ   26

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_SDA   4
#define OLED_SCL   15
#define OLED_RST   -1
#define OLED_ADDR  0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RST);

// ===== Cliente TCP APRS =====
WiFiClient aprsClient;

// ===== Credenciales (extraídas del is-cfg.json) =====
const char* ssid     = "Ubnt1_Casa_4";
const char* password = "cartago4";

const char* callsign = "Ti0tec5-7";
const char* passcode = "26556";
const char* server   = "noam.aprs2.net";
const int   port     = 14580;

// Variables para estadísticas
unsigned long packetsReceived = 0;
unsigned long lastReconnectAttempt = 0;
const unsigned long RECONNECT_INTERVAL = 30000; // 30 segundos

// ===== Funciones auxiliares =====
void mostrarOLED(String linea1, String linea2 = "", String linea3 = "") {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(linea1);
  display.println(linea2);
  display.println(linea3);
  display.display();
}

bool connectToAPRSIS() {
  if (aprsClient.connect(server, port)) {
    Serial.println("Conectado a APRS-IS");
    mostrarOLED("APRS-IS OK", server);

    // Autenticación APRS-IS
    String userStr = "user " + String(callsign) + 
                     " pass " + String(passcode) + 
                     " vers TTGO-LoRa iGate 1.0 filter r/10/-84/50\n";
    aprsClient.print(userStr);
    return true;
  } else {
    mostrarOLED("Error APRS-IS");
    Serial.println("Fallo conexion APRS-IS");
    return false;
  }
}

// ===== Setup =====
void setup() {
  Serial.begin(115200);

  // --- Reset OLED ---
  pinMode(OLED_RST, OUTPUT);
  digitalWrite(OLED_RST, LOW);
  delay(20);
  digitalWrite(OLED_RST, HIGH);

  // --- OLED ---
  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED no encontrado!");
    for (;;);
  }
  mostrarOLED("LilyGO LoRa32", "v1.6.1 iGate", "Iniciando...");

  // --- LoRa ---
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  LoRa.setPins(LORA_CS, LORA_RST, LORA_IRQ);

  if (!LoRa.begin(433.775E6)) {  // frecuencia fija según tu JSON
    mostrarOLED("Error LoRa!", "Revisa conexiones");
    Serial.println("Error iniciando LoRa!");
    while (1);
  }
  Serial.println("LoRa iniciado correctamente");
  mostrarOLED("LoRa OK", "433.775 MHz");

  // --- WiFi ---
  WiFi.begin(ssid, password);
  mostrarOLED("Conectando WiFi", ssid);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  mostrarOLED("WiFi OK", WiFi.localIP().toString());
  Serial.println("WiFi conectado!");
  Serial.println(WiFi.localIP());

  // --- Conexión APRS-IS ---
  connectToAPRSIS();
}

// ===== Procesar paquete LoRa =====
void processLoRaPacket(int packetSize) {
  if (packetSize == 0) return;

  // Leer el paquete
  String packet = "";
  for (int i = 0; i < packetSize; i++) {
    packet += (char)LoRa.read();
  }

  // Mostrar información en consola
  Serial.print("Paquete recibido: ");
  Serial.println(packet);
  Serial.print("RSSI: ");
  Serial.println(LoRa.packetRssi());
  Serial.print("SNR: ");
  Serial.println(LoRa.packetSnr());

  packetsReceived++;

  // Mostrar en OLED
  String rssiStr = "RSSI: " + String(LoRa.packetRssi()) + " dBm";
  String packetsStr = "Paquetes: " + String(packetsReceived);
  mostrarOLED("Paquete recibido", rssiStr, packetsStr);

  // Enviar a APRS-IS si estamos conectados
  if (aprsClient.connected()) {
    if (!packet.endsWith("\n")) packet += "\n";
    aprsClient.print(packet);
    Serial.println("Enviado a APRS-IS: " + packet);
  } else {
    Serial.println("No conectado a APRS-IS, no se puede enviar");
  }
}

// ===== Loop =====
void loop() {
  // Paquetes LoRa
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    processLoRaPacket(packetSize);
  }

  // Verificar conexión APRS-IS
  if (!aprsClient.connected()) {
    if (millis() - lastReconnectAttempt > RECONNECT_INTERVAL) {
      lastReconnectAttempt = millis();
      Serial.println("Intentando reconectar a APRS-IS...");
      connectToAPRSIS();
    }
  } else {
    if (aprsClient.available()) {
      String line = aprsClient.readStringUntil('\n');
      Serial.println("Servidor: " + line);
    }
  }

  delay(10);
}

