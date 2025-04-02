#include <WiFi.h>
#include <HTTPClient.h>
#include <Adafruit_Fingerprint.h>
#include <ESP32Servo.h>
#include <DHT.h>
#include <ArduinoJson.h>

#define SERVO_PUERTA 26
#define SERVO_VENTANA_1 27
#define SERVO_VENTANA_2 14
#define LED_VERDE_PUERTA 21
#define LED_ROJO_PUERTA 22
#define LED_VERDE_VENTANA 19
#define LED_ROJO_VENTANA 23
#define RELE_PIN 25
#define LUZ_PIN 34
#define DHTPIN 18
#define DHTTYPE DHT11

const char* ssid = "beto";
const char* password = "bebeto12";
const char* serverNameStart = "https://smar-edu-suite-backend.vercel.app/iot/huella/startClass";
const char* serverNameClose = "https://smar-edu-suite-backend.vercel.app/iot/huella/closeClass";
const char* serverNameLuz = "https://smar-edu-suite-backend.vercel.app/iot/luz/luzRegistro";
const char* serverNameTemperatura = "https://smar-edu-suite-backend.vercel.app/iot/temperatura/tempRegistro";

const int idSalon = 14;

HardwareSerial huella_serial(2);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&huella_serial);
Servo servoPuerta, servoVentana1, servoVentana2;
DHT dht(DHTPIN, DHTTYPE);

bool claseActiva = false;
String claseId;
unsigned long previousMillis = 0;  // Para el temporizador
const long interval = 60000;       // Intervalo de 1 minuto

// Variables para rastrear el estado anterior de los actuadores
int lastFocoState = -1;     // Estado anterior del foco
int lastVentanaState = -1;  // Estado anterior de las ventanas

void manejarVentanas();
void manejarRele();
bool llamarAPI(const char* url, uint8_t huella, String& claseId);
void verificarHuella();
void registrarDatosSensores();
void registrarEvento(const char* url, int clase, int foco, int ventana, int porcentaje);

void setup() {
  pinMode(LED_VERDE_PUERTA, OUTPUT);
  pinMode(LED_ROJO_PUERTA, OUTPUT);
  pinMode(LED_VERDE_VENTANA, OUTPUT);
  pinMode(LED_ROJO_VENTANA, OUTPUT);
  pinMode(RELE_PIN, OUTPUT);
  servoPuerta.attach(SERVO_PUERTA);
  servoVentana1.attach(SERVO_VENTANA_1);
  servoVentana2.attach(SERVO_VENTANA_2);

  // Inicializar todos los servos en posición 0
  servoPuerta.write(90);     // Puerta cerrada
  servoVentana1.write(90);  // Ventanas cerradas
  servoVentana2.write(0);   // Ventanas cerradas

  Serial.begin(9600);
  huella_serial.begin(57600, SERIAL_8N1, 16, 17);
  dht.begin();

  // Conectar a Wi-Fi
  Serial.print("Conectando a WiFi");
  WiFi.begin(ssid, password);
  int attempts = 0;                                         // Contador de intentos
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {  // Limite a 20 intentos
    delay(500);
    Serial.print(".");
    attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConectado a la red WiFi");
  } else {
    Serial.println("\nNo se pudo conectar a la red WiFi. Verifica las credenciales.");
    while (1) { delay(1000); }  // Detener el programa si no se conecta
  }

  // In iciar el sensor de huellas digitales
  if (finger.verifyPassword()) {
    Serial.println("¡Sensor de huellas digitales encontrado!");
  } else {
    Serial.println("Sensor de huellas digitales no encontrado :(");
    while (1) { delay(1); }
  }
}

void loop() {
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    registrarDatosSensores();  // Registrar datos de sensores cada minuto
  }

  verificarHuella();  // Siempre verificar la huella

  // Solo manejar actuadores si hay una clase activa
  if (claseActiva) {
    manejarVentanas();
    manejarRele();
  }
}

void manejarVentanas() {
  if (claseActiva) {  // Solo manejar ventanas si hay una clase activa
    float temperatura = dht.readTemperature();
    int currentVentanaState = (temperatura > 30) ? 1 : 0;  // 1 si las ventanas deben abrirse, 0 si no

    if (currentVentanaState != lastVentanaState) {
      if (currentVentanaState == 1) {
        digitalWrite(LED_VERDE_VENTANA, HIGH);
        digitalWrite(LED_ROJO_VENTANA, LOW);
        servoVentana1.write(0);   // Abrir ventana 1
        servoVentana2.write(90);  // Abrir ventana 2
        Serial.println("Ventanas abiertas debido a la temperatura.");
        registrarEvento(serverNameLuz, idSalon, 0, 1, 0);  // Registrar evento de abrir ventanas
      } else {
        digitalWrite(LED_VERDE_VENTANA, LOW);
        digitalWrite(LED_ROJO_VENTANA, HIGH);
        servoVentana1.write(90);  // Cerrar ventana 1
        servoVentana2.write(0);   // Cerrar ventana 2
        Serial.println("Ventanas cerradas debido a la temperatura.");
        registrarEvento(serverNameLuz, idSalon, 0, 0, 0);  // Registrar evento de cerrar ventanas
      }
      lastVentanaState = currentVentanaState;  // Actualizar el estado de las ventanas
    }
  }
}

void manejarRele() {
  int currentFocoState = (analogRead(LUZ_PIN) > 3000) ? 1 : 0;  // 1 si el relé debe activarse, 0 si no

  if (currentFocoState != lastFocoState) {
    if (currentFocoState == 1) {
      digitalWrite(RELE_PIN, HIGH);  // Activar el relé
      Serial.println("Relé activado, encendiendo foco");
      registrarEvento(serverNameLuz, idSalon, 1, 0, analogRead(LUZ_PIN));  // Registrar evento de encender luz
    } else {
      digitalWrite(RELE_PIN, LOW);  // Desactivar el relé
      Serial.println("Relé desactivado, apagando foco");
      registrarEvento(serverNameLuz, idSalon, 0, 0, analogRead(LUZ_PIN));  // Registrar evento de apagar luz
    }
    lastFocoState = currentFocoState;  // Actualizar el estado anterior
  }
}


void verificarHuella() {
  Serial.println("Coloca el dedo en el sensor para verificar.");
  uint8_t p = finger.getImage();

  if (p != FINGERPRINT_OK) {
    Serial.println("No se pudo leer la huella, intenta nuevamente.");
    delay(2000);
    return;
  }

  p = finger.image2Tz();
  if (p != FINGERPRINT_OK) {
    Serial.println("No se pudo convertir la imagen, intenta nuevamente.");
    delay(1000);
    return;
  }

  p = finger.fingerSearch();
  if (p == FINGERPRINT_OK) {
    Serial.print("¡ Huella encontrada, ID: ");
    Serial.print(finger.fingerID);
    Serial.println("!");
    if (!claseActiva) {
      // Llamar a la API para iniciar clase
      if (llamarAPI(serverNameStart, finger.fingerID, claseId)) {
        digitalWrite(LED_VERDE_PUERTA, HIGH);
        digitalWrite(LED_ROJO_PUERTA, LOW);
        servoPuerta.write(0);  // Abrir la puerta
        Serial.println("Abriendo la puerta");
        claseActiva = true;

        // Ajustar las ventanas según la temperatura
        manejarVentanas();         // Llama a manejarVentanas para abrir/cerrar ventanas según la temperatura
        registrarDatosSensores();  // Registrar datos al iniciar clase
      } else {
        digitalWrite(LED_VERDE_PUERTA, LOW);
        digitalWrite(LED_ROJO_PUERTA, HIGH);
        Serial.println("Acceso denegado");
      }
    } else {
      // Llamar a la API para cerrar clase
      if (llamarAPI(serverNameClose, finger.fingerID, claseId)) {
        digitalWrite(LED_VERDE_PUERTA, LOW);
        digitalWrite(LED_ROJO_PUERTA, HIGH);

        // Cerrar la puerta primero
        servoPuerta.write(90);
        Serial.println("Cerrando la puerta...");
        delay(1000);  // Esperar 1 segundo antes de seguir

        // Cerrar las ventanas con un pequeño retraso
        Serial.println("Cerrando las ventanas...");
        servoVentana1.write(90);  // Cerrar ventana 1
        servoVentana2.write(0);   // Cerrar ventana 2
        delay(1000);              // Esperar 1 segundo más para estabilizar

        // Apagar el sistema de iluminación
        Serial.println("Apagando iluminación...");
        digitalWrite(RELE_PIN, LOW);

        Serial.println("Clase cerrada correctamente.");
        claseActiva = false;

        // Actualizar los estados
        lastFocoState = 0;
        lastVentanaState = 0;

        registrarDatosSensores();  // Registrar el estado final de sensores
      } else {
        Serial.println("Error al cerrar la clase");
      }
    }
    delay(1000);
    digitalWrite(LED_VERDE_PUERTA, LOW);
    digitalWrite(LED_ROJO_PUERTA, LOW);
  } else {
    Serial.println("Huella no encontrada.");
    digitalWrite(LED_ROJO_PUERTA, HIGH);
    delay(1000);
    digitalWrite(LED_ROJO_PUERTA, LOW);
  }
}

bool llamarAPI(const char* url, uint8_t huella, String& claseId) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    // Crear el JSON para la solicitud
    DynamicJsonDocument doc(256);
    doc["huella"] = String(huella);
    if (url == serverNameStart) {
      doc["idSalon"] = idSalon;
    } else {
      doc["id"] = claseId;
    }

    String requestBody;
    serializeJson(doc, requestBody);

    // Imprimir el JSON que se enviará
    Serial.println("Cuerpo de la solicitud a la API: " + requestBody);

    int httpResponseCode = http.POST(requestBody);

    if (httpResponseCode > 0) {
      String payload = http.getString();
      Serial.println("Respuesta de la API: " + payload);
      http.end();

      // Procesar la respuesta JSON
      DynamicJsonDocument responseDoc(1024);
      deserializeJson(responseDoc, payload);

      // Verificar si la respuesta contiene el mensaje esperado
      if (responseDoc.containsKey("message")) {
        String message = responseDoc["message"].as<String>();
        if (message == "Clase finalizada") {
          return true;  // Clase cerrada exitosamente
        } else if (message == "Clase iniciada") {
          claseId = responseDoc["id"].as<String>();  // Guardar el ID de la clase
          return true;                               // Clase iniciada exitosamente
        }
      }
      return false;  // Error en la respuesta
    } else {
      Serial.println("Error en la solicitud HTTP");
      http.end();
      return false;
    }
  } else {
    Serial.println("No conectado a WiFi");
    return false;
  }
}

void registrarDatosSensores() {
  float temperatura = dht.readTemperature();
  int nivelLuz = analogRead(LUZ_PIN);
  int clase = claseActiva ? 1 : 2;  // 1 si la clase está activa, 2 si no

  // Obtener el estado de las ventanas
  int estadoVentana1 = (servoVentana1.read() <= 80) ? 1 : 0;  // 1 si está abierta, 0 si está cerrada
  int estadoVentana2 = (servoVentana2.read() >= 80) ? 1 : 0;  // 1 si está abierta, 0 si está cerrada

  // Imprimir los valores que se van a enviar
  Serial.print("Registrando datos: ");
  Serial.print("Temperatura: ");
  Serial.print(temperatura);
  Serial.print(", Nivel de Luz: ");
  Serial.print(nivelLuz);
  Serial.print(", Clase: ");
  Serial.println(clase);
  Serial.print(", Estado Ventana 1: ");
  Serial.print(estadoVentana1);
  Serial.print(", Estado Ventana 2: ");
  Serial.println(estadoVentana2);

  // Registrar temperatura
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(serverNameTemperatura);
    http.addHeader("Content-Type", "application/json");
    DynamicJsonDocument doc(256);
    doc["idSalon"] = idSalon;
    doc["clase"] = clase;
    doc["ventana"] = estadoVentana1;  // Enviar estado de la ventana 1
    doc["temperatura"] = temperatura;

    String requestBody;
    serializeJson(doc, requestBody);

    // Imprimir el JSON que se enviará
    Serial.println("Cuerpo de la solicitud de temperatura: " + requestBody);

    int httpResponseCode = http.POST(requestBody);

    if (httpResponseCode > 0) {
      String payload = http.getString();
      Serial.println("Respuesta de la API de temperatura: " + payload);
    } else {
      Serial.println("Error al registrar temperatura");
    }
    http.end();
  }

  // Registrar luz
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(serverNameLuz);
    http.addHeader("Content-Type", "application/json");

    DynamicJsonDocument doc(256);
    doc["idSalon"] = idSalon;
    doc["clase"] = clase;
    doc["foco"] = (lastFocoState == 1) ? 1 : 0;  // Enviar estado del foco
    doc["porcentaje"] = nivelLuz;                // Asegúrate de que el porcentaje se envíe correctamente

    String requestBody;
    serializeJson(doc, requestBody);

    // Imprimir el JSON que se enviará
    Serial.println("Cuerpo de la solicitud de luz: " + requestBody);

    int httpResponseCode = http.POST(requestBody);

    if (httpResponseCode > 0) {
      String payload = http.getString();
      Serial.println("Respuesta de la API de luz: " + payload);
    } else {
      Serial.println("Error al registrar luz");
    }
    http.end();
  }
}

void registrarEvento(const char* url, int idSalon, int foco, int ventana, int porcentaje) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(url);
    http.addHeader("Content-Type", "application/json");

    int clase = claseActiva ? 1 : 2;  // 1 si la clase está activa, 2 si no

    DynamicJsonDocument doc(256);
    doc["idSalon"] = idSalon;
    doc["clase"] = clase;  // Agregar el campo clase
    doc["foco"] = foco;
    doc["ventana"] = ventana;  // Asegúrate de incluir el estado de la ventana
    doc["porcentaje"] = porcentaje;

    // Imprimir el JSON para verificar que todos los campos están presentes
    String requestBody;
    serializeJson(doc, requestBody);  // CORRECCIÓN AQUÍ
    Serial.println("Enviando JSON: " + requestBody);

    int httpResponseCode = http.POST(requestBody);

    if (httpResponseCode > 0) {
      String payload = http.getString();
      Serial.println("Respuesta de la API de evento: " + payload);
    } else {
      Serial.println("Error al registrar evento");
    }
    http.end();
  }
}