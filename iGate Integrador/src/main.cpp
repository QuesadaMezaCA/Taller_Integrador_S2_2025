#include <Arduino.h>
#include <WiFi.h>
#include <SPI.h>
#include <LoRa.h>

// ===== CONFIGURACIÓN APRS =====
const char* callsign = "Ti0tec5-7";   
const char* passcode = "26556";       
const char* server   = "noam.aprs2.net";
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
const unsigned long BEACON_INTERVAL = 120000; // 2 minutos
unsigned long lastBeaconTime = 0;

WiFiClient aprsClient;
unsigned long packetsReceived = 0;
unsigned long lastReconnectAttempt = 0;
const unsigned long RECONNECT_INTERVAL = 30000;

bool connectToAPRSIS() {
  Serial.print("Conectando a ");
  Serial.print(server);
  Serial.print(":");
  Serial.println(port);
  
  if (aprsClient.connect(server, port)) {
    Serial.println("✓ Conectado a APRS-IS");
    
    // Esperar respuesta inicial del servidor
    delay(1000);
    while (aprsClient.available()) {
      String response = aprsClient.readStringUntil('\n');
      Serial.println("Servidor: " + response);
    }
    
    // Autenticación APRS-IS
    String auth = "user " + String(callsign) + " pass " + String(passcode) + " vers TTGO-LoRa-iGate 1.0\n";
    Serial.print("Enviando auth: ");
    Serial.print(auth);
    
    aprsClient.print(auth);
    
    // Esperar y mostrar respuesta de autenticación
    delay(2000);
    bool authSuccess = false;
    unsigned long authStart = millis();
    while (millis() - authStart < 5000) {
      if (aprsClient.available()) {
        String response = aprsClient.readStringUntil('\n');
        Serial.println("Auth response: " + response);
        if (response.indexOf("logged in") >= 0 || response.indexOf("server") >= 0) {
          authSuccess = true;
        }
      }
      delay(100);
    }
    
    if (authSuccess) {
      Serial.println("✓ Autenticación exitosa");
    } else {
      Serial.println("✗ Problema con autenticación");
    }
    
    return true;
  } else {
    Serial.println("✗ Fallo conexión APRS-IS");
    return false;
  }
}

void sendBeacon() {
  if (!aprsClient.connected()) {
    Serial.println("No conectado a APRS-IS, no se puede enviar beacon");
    return;
  }
  
  Serial.println("Preparando beacon...");
  
  // Formatear latitud (DDMM.MMN)
  int lat_deg = abs((int)BEACON_LAT);
  float lat_min = (abs(BEACON_LAT) - lat_deg) * 60.0;
  char lat_dir = BEACON_LAT >= 0 ? 'N' : 'S';
  
  // Formatear longitud (DDDMM.MME)
  int lon_deg = abs((int)BEACON_LON);
  float lon_min = (abs(BEACON_LON) - lon_deg) * 60.0;
  char lon_dir = BEACON_LON >= 0 ? 'E' : 'W';
  
  // Construir paquete beacon - formato simplificado
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
  
  Serial.print("Enviando beacon: ");
  Serial.print(beaconPacket);
  
  int bytesSent = aprsClient.print(beaconPacket);
  Serial.print("Bytes enviados: ");
  Serial.println(bytesSent);
  
  // Verificar si se envió correctamente
  if (bytesSent > 0) {
    Serial.println("✓ Beacon enviado correctamente");
  } else {
    Serial.println("✗ Error enviando beacon");
  }
  
  lastBeaconTime = millis();
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("=== INICIANDO iGATE APRS ===");
  Serial.println("Callsign: " + String(callsign));
  
  // --- WiFi ---
  Serial.println("Conectando WiFi: " + String(ssid));
  WiFi.begin(ssid, password);
  
  unsigned long wifiTimeout = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiTimeout < 15000) {
    delay(500);
    Serial.print(".");
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✓ WiFi conectado!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n✗ Error WiFi");
    return;
  }
  
  // --- LoRa ---
  Serial.println("Iniciando LoRa...");
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  LoRa.setPins(LORA_CS, LORA_RST, LORA_IRQ);
  
  if (!LoRa.begin(LORA_BAND)) {
    Serial.println("✗ Error iniciando LoRa!");
    return;
  }
  
  LoRa.setSpreadingFactor(12);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.setSyncWord(0x12);
  LoRa.enableCrc();
  Serial.println("✓ LoRa iniciado");
  
  // --- APRS-IS ---
  if (connectToAPRSIS()) {
    delay(2000);
    Serial.println("Enviando primer beacon...");
    sendBeacon();
  }
}

void processLoRaPacket(int packetSize) {
  if (packetSize == 0) return;
  
  String packet = "";
  for (int i = 0; i < packetSize; i++) {
    packet += (char)LoRa.read();
  }
  
  Serial.print("LoRa RX: ");
  Serial.println(packet);
  
  packetsReceived++;
  
  if (aprsClient.connected()) {
    String aprsPacket = packet;
    if (!aprsPacket.endsWith("\n")) {
      aprsPacket += "\n";
    }
    
    aprsClient.print(aprsPacket);
    Serial.println("Enviado a APRS-IS");
  }
}

void loop() {
  // Procesar LoRa
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    processLoRaPacket(packetSize);
  }
  
  // Mantener conexión APRS-IS
  if (!aprsClient.connected()) {
    Serial.println("Desconectado de APRS-IS");
    if (millis() - lastReconnectAttempt > RECONNECT_INTERVAL) {
      lastReconnectAttempt = millis();
      if (connectToAPRSIS()) {
        delay(2000);
        sendBeacon();
      }
    }
  } else {
    // Leer respuestas del servidor
    if (aprsClient.available()) {
      String response = aprsClient.readStringUntil('\n');
      Serial.println("APRS-IS: " + response);
    }
    
    // Beacon periódico
    if (millis() - lastBeaconTime > BEACON_INTERVAL) {
      Serial.println("Intervalo de beacon alcanzado");
      sendBeacon();
    }
  }
  
  // Indicador de actividad cada 30 segundos
  static unsigned long lastStatus = 0;
  if (millis() - lastStatus > 30000) {
    lastStatus = millis();
    Serial.println("Status: Conectado=" + String(aprsClient.connected()) + 
                   ", PaquetesRX=" + String(packetsReceived));
  }
  
  delay(100);
}

