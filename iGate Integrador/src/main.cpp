#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <SPI.h>
#include <LoRa.h>  // Librería LoRa de Sandeep Mistry

// ===== CONFIGURACIÓN APRS =====
const char* callsign = "Ti0tec5-7";   
const char* passcode = "26556";       
const char* server   = "noam.aprs2.net";
const int   port     = 14580;

// ===== CONFIGURACIÓN WIFI =====
const char* ssid     = "Ubnt1_Casa_4";
const char* password = "cartago4";

// ===== Cliente TCP APRS =====
WiFiClient aprsClient;

// Variables para estadísticas
unsigned long packetsReceived = 0;
unsigned long lastReconnectAttempt = 0;
const unsigned long RECONNECT_INTERVAL = 30000; // 30 segundos

bool connectToAPRSIS() {
  if (aprsClient.connect(server, port)) {
    Serial.println("Conectado a APRS-IS");

    // Autenticación APRS-IS
    String userStr = "user " + String(callsign) + " pass " + String(passcode) + " vers TTGO-LoRa iGate 1.0 filter r/10/-84/50\n";
    aprsClient.print(userStr);
    return true;
  } else {
    Serial.println("Fallo conexion APRS-IS");
    return false;
  }
}

void setup() {
  Serial.begin(115200);

  // --- LoRa ---
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  LoRa.setPins(LORA_CS, LORA_RST, LORA_IRQ);
  
  // Configurar parámetros LoRa para APRS
  LoRa.setSpreadingFactor(12);         // SF12 (más lento pero mayor alcance)
  LoRa.setSignalBandwidth(125E3);      // 125 kHz
  LoRa.setCodingRate4(5);              // CR 4/5
  LoRa.setSyncWord(0x12);              // Palabra de sincronización para APRS
  LoRa.enableCrc();                    // Habilitar CRC
  
  Serial.println("LoRa iniciado correctamente");

  // --- WiFi ---
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("WiFi conectado!");
  Serial.println(WiFi.localIP());

  // --- Conexión APRS-IS ---
  connectToAPRSIS();
}

void processLoRaPacket(int packetSize) {
  if (packetSize == 0) return;
  
  // Leer el paquete
  String packet = "";
  for (int i = 0; i < packetSize; i++) {
    packet += (char)LoRa.read();
  }
  
  // Mostrar información del paquete
  Serial.print("Paquete recibido: ");
  Serial.println(packet);
  Serial.print("RSSI: ");
  Serial.println(LoRa.packetRssi());
  Serial.print("SNR: ");
  Serial.println(LoRa.packetSnr());
  
  // Actualizar estadísticas
  packetsReceived++;
  
  // Mostrar en OLED
  String rssiStr = "RSSI: " + String(LoRa.packetRssi()) + " dBm";
  String snrStr = "SNR: " + String(LoRa.packetSnr()) + " dB";
  String packetsStr = "Paquetes: " + String(packetsReceived);
  
  // Enviar a APRS-IS si estamos conectados
  if (aprsClient.connected()) {
    // Añadir información de RF antes de enviar (opcional para aprs.fi)
    String aprsPacket = packet;
    if (!aprsPacket.endsWith("\n")) {
      aprsPacket += "\n";
    }
    
    aprsClient.print(aprsPacket);
    Serial.println("Enviado a APRS-IS: " + aprsPacket);
  } else {
    Serial.println("No conectado a APRS-IS, no se puede enviar");
  }
}

void loop() {
  // Verificar si hay paquetes LoRa disponibles
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    processLoRaPacket(packetSize);
  }
  
  // Verificar conexión APRS-IS
  if (!aprsClient.connected()) {
    // Intentar reconectar periódicamente
    if (millis() - lastReconnectAttempt > RECONNECT_INTERVAL) {
      lastReconnectAttempt = millis();
      Serial.println("Intentando reconectar a APRS-IS...");
      connectToAPRSIS();
    }
  } else {
    // Leer datos del servidor (para mantener conexión activa)
    if (aprsClient.available()) {
      String line = aprsClient.readStringUntil('\n');
      Serial.println("Servidor: " + line);
    }
  }
  
  delay(10);
}