#include <Arduino.h>
#include <WiFi.h>
#include <SPI.h>
#include <LoRa.h>
#include <algorithm>  // Necesario para std::min

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
const unsigned long BEACON_INTERVAL = 180000; // 3 minutos
const unsigned long STATUS_INTERVAL = 180000;
unsigned long lastBeaconTime = 0;
unsigned long lastStatusTime = 0;

WiFiClient aprsClient;
unsigned long packetsReceived = 0;
unsigned long packetsSentToAPRSIS = 0;
unsigned long packetsReceivedFromAPRSIS = 0;
unsigned long lastReconnectAttempt = 0;
const unsigned long RECONNECT_INTERVAL = 30000;

// Función para mostrar timestamp
String getTimestamp() {
  unsigned long seconds = millis() / 1000;
  unsigned long minutes = seconds / 60;
  seconds %= 60;
  minutes %= 60;
  char timestamp[12];
  sprintf(timestamp, "[%02lu:%02lu] ", minutes, seconds);
  return String(timestamp);
}

bool connectToAPRSIS() {
  Serial.print(getTimestamp());
  Serial.print("Conectando a ");
  Serial.print(server);
  Serial.print(":");
  Serial.println(port);
  
  if (aprsClient.connect(server, port)) {
    Serial.println(getTimestamp() + "✓ Conectado a APRS-IS");
    
    // Esperar respuesta inicial del servidor
    delay(1000);
    while (aprsClient.available()) {
      String response = aprsClient.readStringUntil('\n');
      Serial.println(getTimestamp() + "SRV_INIT: " + response);
    }
    
    // Autenticación APRS-IS
    String auth = "user " + String(callsign) + " pass " + String(passcode) + " vers TTGO-LoRa-iGate 1.0\n";
    Serial.print(getTimestamp() + "AUTH_SEND: ");
    Serial.print(auth);
    
    aprsClient.print(auth);
    
    // Esperar y mostrar respuesta de autenticación
    delay(2000);
    bool authSuccess = false;
    unsigned long authStart = millis();
    while (millis() - authStart < 5000) {
      if (aprsClient.available()) {
        String response = aprsClient.readStringUntil('\n');
        Serial.println(getTimestamp() + "AUTH_RESP: " + response);
        if (response.indexOf("logged in") >= 0 || response.indexOf("server") >= 0 || response.indexOf("verified") >= 0) {
          authSuccess = true;
        }
      }
      delay(100);
    }
    
    if (authSuccess) {
      Serial.println(getTimestamp() + "✓ Autenticación exitosa");
      
      // PROBAR DIFERENTES FILTROS - MÁS AMPLIOS
      delay(1000);
      
      // Opción 1: Filtro de área amplio (200km)
      String filter1 = "filter r/9.85/-83.90/200\n";
      aprsClient.print(filter1);
      Serial.println(getTimestamp() + "FILTRO_1: " + filter1);
      delay(500);
      
      Serial.println(getTimestamp() + "✓ Filtros aplicados, esperando tráfico...");
      
    } else {
      Serial.println(getTimestamp() + "✗ Problema con autenticación");
    }
    
    return true;
  } else {
    Serial.println(getTimestamp() + "✗ Fallo conexión APRS-IS");
    return false;
  }
}

void sendBeacon() {
  if (!aprsClient.connected()) {
    Serial.println(getTimestamp() + "No conectado a APRS-IS, no se puede enviar beacon");
    return;
  }
  
  Serial.println(getTimestamp() + "Preparando beacon...");
  
  // Formatear latitud (DDMM.MMN)
  int lat_deg = abs((int)BEACON_LAT);
  float lat_min = (abs(BEACON_LAT) - lat_deg) * 60.0;
  char lat_dir = BEACON_LAT >= 0 ? 'N' : 'S';
  
  // Formatear longitud (DDDMM.MME)
  int lon_deg = abs((int)BEACON_LON);
  float lon_min = (abs(BEACON_LON) - lon_deg) * 60.0;
  char lon_dir = BEACON_LON >= 0 ? 'E' : 'W';
  
  // Construir paquete beacon
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
  
  Serial.print(getTimestamp() + "BEACON_TX: ");
  Serial.print(beaconPacket);
  
  int bytesSent = aprsClient.print(beaconPacket);
  Serial.print(getTimestamp() + "BEACON_BYTES: ");
  Serial.println(bytesSent);
  
  if (bytesSent > 0) {
    Serial.println(getTimestamp() + "✓ Beacon enviado correctamente");
    
    // MOSTRAR STATUS INMEDIATAMENTE DESPUÉS DEL BEACON
    Serial.println(getTimestamp() + "📊 STATUS: " + 
                   "APRS-IS=" + (aprsClient.connected() ? "CONECTADO" : "DESCONECTADO") +
                   ", LoRa_RX=" + String(packetsReceived) +
                   ", APRS_TX=" + String(packetsSentToAPRSIS) +
                   ", APRS_RX=" + String(packetsReceivedFromAPRSIS));
  } else {
    Serial.println(getTimestamp() + "✗ Error enviando beacon");
  }
  
  lastBeaconTime = millis();
  lastStatusTime = millis(); // Resetear también el contador de status
}

void processAPRSTraffic() {
  static String buffer = "";
  
  while (aprsClient.available()) {
    char c = aprsClient.read();
    buffer += c;
    
    // Procesar línea completa
    if (c == '\n') {
      buffer.trim();
      
      if (buffer.length() > 0) {
        // Filtrar mensajes del sistema que empiezan con #
        if (buffer.charAt(0) == '#') {
          // MOSTRAR TODOS los mensajes del sistema para debug
          Serial.println(getTimestamp() + "SRV_SYS: " + buffer);
          
          // Verificar si el servidor aceptó nuestros filtros
          if (buffer.indexOf("filter") >= 0) {
            Serial.println(getTimestamp() + "💡 FILTRO_ACEPTADO: " + buffer);
          }
        } 
        // Filtrar mensajes de error
        else if (buffer.indexOf("invalid") >= 0 || buffer.indexOf("error") >= 0) {
          Serial.println(getTimestamp() + "SRV_ERR: " + buffer);
        }
        // Mostrar paquetes APRS normales
        else {
          packetsReceivedFromAPRSIS++;
          Serial.println(getTimestamp() + "🎯 APRS_RX [" + String(packetsReceivedFromAPRSIS) + "]: " + buffer);
          
          // Mostrar información adicional para paquetes de posición
          if (buffer.indexOf("!") >= 0 || buffer.indexOf("=") >= 0) {
            // Extraer callsign del remitente
            int arrowPos = buffer.indexOf('>');
            if (arrowPos > 0) {
              String sender = buffer.substring(0, arrowPos);
              Serial.println(getTimestamp() + "       From: " + sender);
              
              // Mostrar distancia aproximada si tenemos datos de posición
              int posIdx = buffer.indexOf('!');
              if (posIdx == -1) posIdx = buffer.indexOf('=');
              if (posIdx > 0) {
                  int endPos = posIdx + 20;
                  if (endPos > buffer.length()) {
                      endPos = buffer.length();
                  }
                  Serial.println(getTimestamp() + "       Posición: " + buffer.substring(posIdx, endPos));
              }
            }
          }
        }
        
        buffer = "";
      }
    }
    
    // Limitar tamaño del buffer por seguridad
    if (buffer.length() > 200) {
      Serial.println(getTimestamp() + "BUFFER_OVERFLOW: " + buffer);
      buffer = "";
    }
  }
}

void processLoRaPacket(int packetSize) {
  if (packetSize == 0) return;
  
  String packet = "";
  for (int i = 0; i < packetSize; i++) {
    packet += (char)LoRa.read();
  }
  
  packetsReceived++;
  
  // Mostrar información detallada del paquete LoRa
  Serial.println(getTimestamp() + "════════════════════════════════════════");
  Serial.println(getTimestamp() + "LORA_RX [" + String(packetsReceived) + "]: " + packet);
  Serial.print(getTimestamp() + "RSSI: ");
  Serial.print(LoRa.packetRssi());
  Serial.print(" dBm, SNR: ");
  Serial.print(LoRa.packetSnr());
  Serial.print(" dB, Size: ");
  Serial.println(packetSize);
  
  // Enviar a APRS-IS si estamos conectados
  if (aprsClient.connected()) {
    String aprsPacket = packet;
    if (!aprsPacket.endsWith("\n")) {
      aprsPacket += "\n";
    }
    
    int bytesSent = aprsClient.print(aprsPacket);
    if (bytesSent > 0) {
      packetsSentToAPRSIS++;
      
      // Mostrar preview del paquete (máximo 60 caracteres)
      int previewLength = packet.length();
      if (previewLength > 60) {
        previewLength = 60;
      }
      Serial.println(getTimestamp() + "APRS_TX [" + String(packetsSentToAPRSIS) + "]: " + packet.substring(0, previewLength) + "...");
      Serial.println(getTimestamp() + "✓ Enviado a APRS-IS (" + String(bytesSent) + " bytes)");
    } else {
      Serial.println(getTimestamp() + "✗ Error enviando a APRS-IS");
    }
  } else {
    Serial.println(getTimestamp() + "✗ No conectado a APRS-IS, paquete perdido");
  }
  Serial.println(getTimestamp() + "════════════════════════════════════════");
}

void showStatus() {
  Serial.println(getTimestamp() + "📊 STATUS: " + 
                 "APRS-IS=" + (aprsClient.connected() ? "CONECTADO" : "DESCONECTADO") +
                 ", LoRa_RX=" + String(packetsReceived) +
                 ", APRS_TX=" + String(packetsSentToAPRSIS) +
                 ", APRS_RX=" + String(packetsReceivedFromAPRSIS));
  lastStatusTime = millis();
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n" + getTimestamp() + "=== INICIANDO iGATE APRS ===");
  Serial.println(getTimestamp() + "Callsign: " + String(callsign));
  
  // --- WiFi ---
  Serial.println(getTimestamp() + "Conectando WiFi: " + String(ssid));
  WiFi.begin(ssid, password);
  
  unsigned long wifiTimeout = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiTimeout < 15000) {
    delay(500);
    Serial.print(".");
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n" + getTimestamp() + "✓ WiFi conectado!");
    Serial.print(getTimestamp() + "IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n" + getTimestamp() + "✗ Error WiFi");
    return;
  }
  
  // --- LoRa ---
  Serial.println(getTimestamp() + "Iniciando LoRa...");
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  LoRa.setPins(LORA_CS, LORA_RST, LORA_IRQ);
  
  if (!LoRa.begin(LORA_BAND)) {
    Serial.println(getTimestamp() + "✗ Error iniciando LoRa!");
    return;
  }
  
  LoRa.setSpreadingFactor(12);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.setSyncWord(0x12);
  LoRa.enableCrc();
  Serial.println(getTimestamp() + "✓ LoRa iniciado (SF12, BW125kHz)");
  
  // --- APRS-IS ---
  if (connectToAPRSIS()) {
    delay(2000);
    Serial.println(getTimestamp() + "Enviando primer beacon...");
    sendBeacon();
  }
}

void loop() {
  // Procesar paquetes LoRa
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    processLoRaPacket(packetSize);
  }
  
  // Mantener conexión APRS-IS
  if (!aprsClient.connected()) {
    Serial.println(getTimestamp() + "⚠️  Desconectado de APRS-IS");
    if (millis() - lastReconnectAttempt > RECONNECT_INTERVAL) {
      lastReconnectAttempt = millis();
      Serial.println(getTimestamp() + "🔄 Intentando reconexión...");
      if (connectToAPRSIS()) {
        delay(2000);
        sendBeacon();
      }
    }
  } else {
    // Procesar tráfico entrante de APRS-IS
    processAPRSTraffic();
    
    // Beacon periódico cada 3 minutos
    if (millis() - lastBeaconTime > BEACON_INTERVAL) {
      Serial.println(getTimestamp() + "⏰ Intervalo de beacon alcanzado");
      sendBeacon(); // Esta función ahora también muestra el STATUS
    }
    
    // Status independiente cada 3 minutos (si no se mostró con el beacon)
    if (millis() - lastStatusTime > STATUS_INTERVAL) {
      showStatus();
    }
  }
  
  delay(50);
}