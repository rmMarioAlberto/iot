#include <Adafruit_Fingerprint.h>

// Configuración de pines para el ESP32
#define RX_PIN 16
#define TX_PIN 17

// Inicialización del puerto serial para el sensor de huellas
HardwareSerial huella_serial(2); // Usamos el UART2 del ESP32
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&huella_serial);

void setup() {
  Serial.begin(9600);
  delay(100);
  Serial.println("\n\nAdafruit Fingerprint sensor");

  // Configurar la velocidad de datos para el puerto serial del sensor
  huella_serial.begin(57600, SERIAL_8N1, RX_PIN, TX_PIN);

  if (finger.verifyPassword()) {
    Serial.println("¡Sensor de huellas digitales encontrado!");
  } else {
    Serial.println("No se encontró el sensor de huellas digitales :(");
    while (1) { delay(1); }
  }
}

void loop() {
  int id = getFingerprintID();
  if (id == -1) {
    Serial.println("No se pudo leer la huella, intenta nuevamente.");
  } else if (id == -2) {
    Serial.println("Huella no encontrada.");
    Serial.println("¿Deseas registrar esta huella? (s/n)");
    
    // Esperar una respuesta válida del usuario
    while (!Serial.available());
    char response = Serial.read();
    while (response != 's' && response != 'S' && response != 'n' && response != 'N') {
      Serial.println("Respuesta no válida. Por favor, ingresa 's' para sí o 'n' para no.");
      while (!Serial.available());
      response = Serial.read();
    }

    if (response == 's' || response == 'S') {
      Serial.println("Por favor, escribe el ID # (de 1 a 127) donde quieres guardar esta huella...");
      uint8_t newID = readnumber();
      if (newID != 0) { // ID #0 no permitido
        if (getFingerprintEnroll(newID)) {
          Serial.println("¡Huella registrada exitosamente!");
        } else {
          Serial.println("Error al registrar la huella.");
        }
      }
    }
  } else {
    Serial.print("¡Huella encontrada, ID: ");
    Serial.print(id);
    Serial.println("!");
  }
  delay(1000); // Esperar un segundo antes de la siguiente lectura
}

int getFingerprintID() {
  uint8_t p = finger.getImage();
  if (p != FINGERPRINT_OK) return -1;

  p = finger.image2Tz();
  if (p != FINGERPRINT_OK) return -1;

  p = finger.fingerFastSearch();
  if (p != FINGERPRINT_OK) return -2;

  return finger.fingerID;
}

uint8_t readnumber(void) {
  uint8_t num = 0;

  while (num == 0) {
    while (!Serial.available());
    num = Serial.parseInt();
  }
  return num;
}

uint8_t getFingerprintEnroll(uint8_t id) {
  int p = -1;
  Serial.print("Esperando un dedo válido para registrar como #"); Serial.println(id);
  while (p != FINGERPRINT_OK) {
    p = finger.getImage();
    switch (p) {
      case FINGERPRINT_OK:
        Serial.println("Imagen tomada");
        break;
      case FINGERPRINT_NOFINGER:
        Serial.print(".");
        break;
      case FINGERPRINT_PACKETRECIEVEERR:
        Serial.println("Error de comunicación");
        break;
      case FINGERPRINT_IMAGEFAIL:
        Serial.println("Error al tomar la imagen");
        break;
      default:
        Serial.println("Error desconocido");
        break;
    }
  }

  p = finger.image2Tz(1);
  if (p != FINGERPRINT_OK) {
    Serial.println("No se pudieron encontrar características de la huella");
    return p;
  }

  Serial.println("Retira el dedo");
  delay(2000);
  p = 0;
  while (p != FINGERPRINT_NOFINGER) {
    p = finger.getImage();
  }
  Serial.println("Coloca el mismo dedo nuevamente");
  while (p != FINGERPRINT_OK) {
    p = finger.getImage();
    switch (p) {
      case FINGERPRINT_OK:
        Serial.println("Imagen tomada");
        break;
      case FINGERPRINT_NOFINGER:
        Serial.print(".");
        break;
      case FINGERPRINT_PACKETRECIEVEERR:
        Serial.println("Error de comunicación");
        break;
      case FINGERPRINT_IMAGEFAIL:
        Serial.println("Error al tomar la imagen");
        break;
      default:
        Serial.println("Error desconocido");
        break;
    }
  }

  p = finger.image2Tz(2);
  if (p != FINGERPRINT_OK) {
    Serial.println("No se pudieron encontrar características de la huella");
    return p;
  }

  Serial.print("Creando modelo para #");  Serial.println(id);

  p = finger.createModel();
  if (p != FINGERPRINT_OK) {
    Serial.println("Las huellas no coinciden");
    return p;
  }

  p = finger.storeModel(id);
  if (p != FINGERPRINT_OK) {
    Serial.println("Error al almacenar la huella");
    return p;
  }

  return true;
} 