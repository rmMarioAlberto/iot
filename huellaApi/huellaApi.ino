#include <Adafruit_Fingerprint.h>
#include <ESP32Servo.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#define SERVO_PIN 26
#define LED_VERDE_PUERTA 21
#define LED_ROJO_PUERTA 22
#define RX_PIN 16
#define TX_PIN 17
#define RX_GSM 3
#define TX_GSM 1

const char* ssid = "MEGACABLE-2.4G-FEAB";
const char* password = "2NyZbduaAC";
const char* apiEndpoint = "https://smar-edu-suite-backend.vercel.app/iot/huella/registrarHuella";

HardwareSerial huella_serial(2);
HardwareSerial gsmSerial(1);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&huella_serial);
Servo servo;
WebServer server(80);

bool registrandoHuella = false;
int maestroID = 0;
String estadoActual = "idle";
uint8_t huellaID = 0;
String numeroOrigen = "";

void setup() {
  pinMode(LED_VERDE_PUERTA, OUTPUT);
  pinMode(LED_ROJO_PUERTA, OUTPUT);
  digitalWrite(LED_VERDE_PUERTA, LOW);
  digitalWrite(LED_ROJO_PUERTA, LOW);

  servo.attach(SERVO_PIN);
  servo.write(0);

  Serial.begin(115200);
  huella_serial.begin(57600, SERIAL_8N1, RX_PIN, TX_PIN);
  gsmSerial.begin(9600, SERIAL_8N1, RX_GSM, TX_GSM);
  delay(100);

  Serial.println("Iniciando diagnóstico de GSM...");
  gsmSerial.println("AT");
  delay(1000);
  while (gsmSerial.available()) {
    String respuesta = gsmSerial.readStringUntil('\n');
    Serial.println("Respuesta GSM: " + respuesta);
  }

  gsmSerial.println("AT+CSQ");
  delay(500);
  while (gsmSerial.available()) {
    String csq = gsmSerial.readStringUntil('\n');
    Serial.println("Nivel de señal GSM: " + csq);
  }

  gsmSerial.println("AT+CREG?");
  delay(500);
  while (gsmSerial.available()) {
    String creg = gsmSerial.readStringUntil('\n');
    Serial.println("Estado de red GSM: " + creg);
  }

  finger.begin(57600);
  if (finger.verifyPassword()) {
    Serial.println("¡Sensor de huellas encontrado y funcionando!");
  } else {
    Serial.println("Sensor de huellas no encontrado. Verificar conexiones.");
    while (1) { delay(1); }
  }

  conectarWiFi();

  server.on("/api/registrar-huella", HTTP_GET, handleRegistrarHuella);
  server.on("/api/estado", HTTP_GET, getEstado);
  server.on("/api/eliminar-huella", HTTP_GET, handleEliminarHuella);
  server.begin();
  Serial.println("Servidor HTTP disponible en el puerto 80.");

  Serial.println("Envía un SMS con: 'registrar maestro id:123' para iniciar registro.");
}

void loop() {
  server.handleClient();

  if (registrandoHuella) {
    Serial.println("Espere: verificando huella existente por 5 segundos...");
    unsigned long start = millis();
    int existingId = -1;
    while (millis() - start < 5000) {
      existingId = verificarHuellaExistente();
      if (existingId > 0) break;
      delay(200);
    }

    if (existingId > 0) {
      enviarSMS("Huella existente detectada. Enviando a API.", numeroOrigen);
      enviarHuellaAPi(maestroID, existingId);
      registrandoHuella = false;
      estadoActual = "completado";
      enviarSMS("Huella registrada exitosamente en la API.", numeroOrigen);
    } else if (getFingerprintEnroll(huellaID)) {
      delay(1000);
      enviarSMS("Huella capturada. Enviando a API...", numeroOrigen);
      enviarHuellaAPi(maestroID, huellaID);
      registrandoHuella = false;
      estadoActual = "completado";
      enviarSMS("Huella registrada exitosamente en la API.", numeroOrigen);
    } else {
      Serial.println("Fallo al registrar huella.");
      estadoActual = "error";
      registrandoHuella = false;
      enviarSMS("Hubo un error al registrar la huella.", numeroOrigen);
    }
  }

  if (gsmSerial.available()) {
    String linea = gsmSerial.readStringUntil('\n');
    linea.trim();

    if (linea.indexOf("+CMT:") != -1) {
      int startNum = linea.indexOf(",\"") + 2;
      int endNum = linea.indexOf("\"", startNum);
      numeroOrigen = linea.substring(startNum, endNum);
      linea = gsmSerial.readStringUntil('\n');

      Serial.println("SMS recibido de " + numeroOrigen + ": " + linea);

      if (linea.indexOf("registrar maestro") != -1) {
        int indexID = linea.indexOf("id:");
        if (indexID != -1) {
          maestroID = linea.substring(indexID + 3).toInt();
          huellaID = maestroID;
          registrandoHuella = true;
          estadoActual = "registrando";
          enviarSMS("Registro de huella iniciado para ID: " + String(maestroID), numeroOrigen);
        }
      }
    }
  }
}

void enviarSMS(String mensaje, String numero) {
  gsmSerial.println("AT+CMGF=1");
  delay(500);
  gsmSerial.println("AT+CMGS=\"" + numero + "\"");
  delay(500);
  gsmSerial.print(mensaje);
  delay(500);
  gsmSerial.write(26);
  delay(500);
}


void conectarWiFi() {
  WiFi.begin(ssid, password);
  Serial.println("Conectando a WiFi...");
  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 15000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nNo se pudo conectar a WiFi, reiniciando...");
    ESP.restart();
  }

  Serial.println("\nWiFi conectado");
  Serial.print("Dirección IP: ");
  Serial.println(WiFi.localIP());
}

void blinkLED(int pin, int times, int delayTime) {
  for (int i = 0; i < times; i++) {
    digitalWrite(pin, HIGH);
    delay(delayTime);
    digitalWrite(pin, LOW);
    delay(delayTime);
  }
}

void blinkIntercalado(int times, int delayTime) {
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_VERDE_PUERTA, HIGH);
    delay(delayTime);
    digitalWrite(LED_VERDE_PUERTA, LOW);
    digitalWrite(LED_ROJO_PUERTA, HIGH);
    delay(delayTime);
    digitalWrite(LED_ROJO_PUERTA, LOW);
  }
}

void getEstado() {
  DynamicJsonDocument doc(256);
  doc["estado"] = estadoActual;

  String response;
  serializeJson(doc, response);
  server.send(200 , "application/json", response);
}

void handleRegistrarHuella() {
  if (server.hasArg("id")) {
    maestroID = server.arg("id").toInt();
    if (maestroID > 0) {
      huellaID = buscarIDDisponible();
      if (huellaID != 0) {
        blinkLED(LED_VERDE_PUERTA, 5, 200);
        registrandoHuella = true;
        estadoActual = "registrando";
        server.send(200, "text/plain", "Registro de huella iniciado");
      } else {
        server.send(400, "text/plain", "No hay espacio disponible para nuevas huellas");
      }
    } else {
      server.send(400, "text/plain", "ID de maestro no válido");
    }
  } else {
    server.send(400, "text/plain", "ID de maestro no proporcionado");
  }
}

void handleEliminarHuella() {
  if (server.hasArg("idhuella")) {
    int idHuellaEliminar = server.arg("idhuella").toInt();
    if (idHuellaEliminar > 0 && idHuellaEliminar < 128) {
      if (finger.deleteModel(idHuellaEliminar) == FINGERPRINT_OK) {
        server.send(200, "text/plain", "Huella eliminada correctamente");
        blinkLED(LED_VERDE_PUERTA, 3, 200);
      } else {
        server.send(500, "text/plain", "Error al eliminar la huella");
        blinkLED(LED_ROJO_PUERTA, 3, 200);
      }
    } else {
      server.send(400, "text/plain", "ID de huella no válido");
    }
  } else {
    server.send(400, "text/plain", "ID de huella no proporcionado");
  }
}

uint8_t buscarIDDisponible() {
  for (uint8_t i = 1; i < 128; i++) {
    if (finger.loadModel(i) != FINGERPRINT_OK) {
      return i;
    }
  }
  return 0;
}

int verificarHuellaExistente() {
  if (finger.getImage() != FINGERPRINT_OK) return -1;
  if (finger.image2Tz() != FINGERPRINT_OK) return -1;
  if (finger.fingerFastSearch() != FINGERPRINT_OK) return -1;
  return finger.fingerID;
}

uint8_t getFingerprintEnroll(uint8_t id) {
  int p = -1;
  Serial.print("Esperando primer toma de dedo válido para #"); Serial.println(id);

  while (p != FINGERPRINT_OK) {
    p = finger.getImage();
    if (p == FINGERPRINT_NOFINGER) { delay(100); continue; }
    if (p != FINGERPRINT_OK) { blinkLED(LED_ROJO_PUERTA, 3, 150); }
  }
  finger.image2Tz(1);
  blinkLED(LED_VERDE_PUERTA, 3, 150);

  Serial.println("Retira el dedo...");
  delay(2000);
  while (finger.getImage() != FINGERPRINT_NOFINGER) { delay(100); }

  Serial.println("Coloca el mismo dedo nuevamente para segunda toma");
  p = -1;
  while (p != FINGERPRINT_OK) {
    p = finger.getImage();
    if (p == FINGERPRINT_NOFINGER) { delay(100); continue; }
    if (p != FINGERPRINT_OK) { blinkLED(LED_ROJO_PUERTA, 3, 150); }
  }
  finger.image2Tz(2);
  blinkLED(LED_VERDE_PUERTA, 3, 150);

  if (finger.createModel() != FINGERPRINT_OK) {
    blinkLED(LED_ROJO_PUERTA, 3, 200);
    return 0;
  }
  if (finger.storeModel(id) != FINGERPRINT_OK) {
    blinkLED(LED_ROJO_PUERTA, 3, 200);
    return 0;
  }

  return true;
}

void enviarHuellaAPi(int idMaestro, uint8_t idHuella) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi desconectado, reintentando conexión...");
    conectarWiFi();
  }

  HTTPClient http;
  http.begin(apiEndpoint);
  http.addHeader("Content-Type", "application/json");

DynamicJsonDocument doc(256);
  doc["idMaestro"] = idMaestro;
  doc["idhuella"] = idHuella;

  String jsonString;
  serializeJson(doc, jsonString);

  int httpResponseCode = http.POST(jsonString);
  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.println(httpResponseCode);
    Serial.println(response);
    blinkIntercalado(3, 300);
  } else {
    Serial.print("Error en la conexión o envío: ");
    Serial.println(httpResponseCode);
    estadoActual = "error_api";
    blinkLED(LED_ROJO_PUERTA, 5, 200);
  }
  http.end();
}