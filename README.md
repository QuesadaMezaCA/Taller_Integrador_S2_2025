# Proyecto iGate LoRa32 con APRS-IS y OLED

Este proyecto implementa un **iGate APRS** utilizando la placa **LilyGO LoRa32 v1.6.1 (ESP32 + LoRa SX1276)**.  
El dispositivo recibe tramas APRS vía LoRa y las reenvía a **APRS-IS**, además de mostrar en una **pantalla OLED SSD1306** información en tiempo real sobre la conexión, servidor, red WiFi y paquetes RX/TX.


# Arquitectura del Sistema

## Componentes Principales

- Microcontrolador: ESP32 con módulo LoRa

- Comunicación inalámbrica: WiFi + LoRa

- Visualización: Pantalla OLED SSD1306

- Protocolo: APRS-IS para internet, AX.25 sobre LoRa

# Estructura del Código

## 1. Configuración de Hardware

```cpp
// LED de estado
#define LED_PIN 25

// Pantalla OLED (128x64 píxeles)
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C

// Pines LoRa
#define LORA_SCK     5
#define LORA_MISO    19
#define LORA_MOSI    27
#define LORA_CS      18
#define LORA_RST     14
#define LORA_IRQ     26
#define LORA_BAND    433.775E6
```

## 2. Configuración APRS
- Callsign: Identificador único en red APRS

- Servidor: rotate.aprs2.net (puerto 14580)

- Filtro geográfico: Solo paquetes dentro de 200km de la posición

## 3. Flujo de Operación
Inicialización (Setup)
- Inicia comunicación serial (115200 baudios)

- Configura GPIO y LED de estado

- Inicializa pantalla OLED mediante I2C

- Conecta a WiFi con reconexión automática

- Configura módulo LoRa (433.775 MHz, SF7, BW125kHz)

- Conecta a APRS-IS y autentica

- Envía beacon inicial de posición

## Bucle Principal (Loop)
- Escucha paquetes LoRa entrantes

- Verifica conexiones (WiFi + APRS-IS)

- Procesa tráfico APRS-IS desde internet

- Envía beacons periódicos cada 3 minutos

- Actualiza pantalla OLED cada segundo

# Funciones Clave
## Gestión de Conexiones
- connectToWiFi(): Maneja conexión/reconexión WiFi

- connectToAPRSIS(): Establece conexión con servidor APRS

- checkAPRSISConnectionHealth(): Monitorea estado de conexión

## Procesamiento de Datos
- processLoRaPacket(): Decodifica paquetes LoRa (texto/hexadecimal)

- processAPRSTraffic(): Maneja datos entrantes de APRS-IS

- sendBeacon(): Construye y envía paquete de posición

## Interface de Usuario
- updateOLEDStatus(): Muestra estado en tiempo real en pantalla

- getTimestamp(): Genera timestamps para logging

# Características de Robustez
## Reconexión Automática
- WiFi: Verificación cada 10 segundos, reconexión cada 30s

- APRS-IS: Detección de timeout (2 minutos sin tráfico)

- Ping periódico al servidor cada 60 segundos

## Monitoreo de Estado
- LED indicador: Parpadeo durante conexión, estado sólido cuando conectado

- Logging detallado: Timestamps y códigos de error

- Estadísticas: Contadores de paquetes enviados/recibidos

## Estructura de Datos
- Posición: Grados, minutos y dirección

- Comentario: Identificación de estación

- Timestamp: Tiempo de envío automático

## Dependencias y Bibliotecas
- WiFi.h: Gestión de conexión WiFi

- LoRa.h: Comunicación con módulo LoRa

- Adafruit_SSD1306.h: Control de pantalla OLED

- SPI.h: Comunicación con módulo LoRa

- Wire.h: Comunicación I2C con OLED

## Parámetros Configurables
### Intervalos de Tiempo
- Beacon: 3 minutos (180,000 ms)

- Verificación WiFi: 10 segundos

- Timeout APRS: 2 minutos

- Ping servidor: 1 minuto

### Configuración Radio
- Frecuencia: 433.775 MHz

- Spreading Factor: 7

- Ancho de Banda: 125 kHz

- Tasa de Codificación: 4/5

## Estados del Sistema
- OPERATIVO: WiFi + APRS-IS conectados

- WIFI-SOLO: Solo WiFi conectado

- OFFLINE: Sin conexiones activas
