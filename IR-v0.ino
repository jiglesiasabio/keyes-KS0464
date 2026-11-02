/*
  KEYESTUDIO KS0464 - MANDO IR + CARAS, V3.0
  Arduino UNO / ATmega328P, shield con PWM_A en D9.

  Librerias (Gestor de bibliotecas):
    - IRremote 4.x (Armin Joachimsmeyer)
    - Adafruit LED Backpack Library
    - Adafruit GFX Library
    - Adafruit BusIO (dependencia)

  Conexiones:
    Motor A: direccion D2, PWM D9. Motor B: direccion D4, PWM D5.
    Receptor IR: P4 (G, V, A1). NO es el conector de motor A1.
    Matriz: SDA A4, SCL A5, V 5V, G GND; direccion 0x70.

  Mando Keyestudio de 17 botones:
    Flechas mantenidas: avanzar, retroceder y girar sobre su eje.
    OK: parar. 0-9: parar y cambiar la cara.
    *: parar y reducir velocidad. #: parar y aumentar velocidad.
    Sin orden de movimiento durante 300 ms: cortar motores.

  IMPORTANTE:
    No se usa Servo.h: Timer1 debe quedar disponible para PWM en D9.
    IRremote usa Timer2. No anadir tone() ni PWM en D3/D11.
    No hay control del servo ni deteccion automatica de obstaculos.
    Primera prueba con las ruedas levantadas y baterias conectadas.

  Referencias para pines, mando y librerias:
    https://wiki.keyestudio.com/KS0298_Keyestudio_quick_connectors_Motor_Drive_Shield
    https://docs.keyestudio.com/projects/KS0464/en/latest/KS0464_arduino.html
    https://github.com/Arduino-IRremote/Arduino-IRremote
    https://github.com/adafruit/Adafruit_LED_Backpack
*/

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_LEDBackpack.h>

#define DECODE_NEC
#define IR_USE_AVR_TIMER2
#define RAW_BUFFER_LENGTH 100
#include <IRremote.hpp>

// ================= CONFIGURACION =================
const uint8_t DIR_A = 2;
const uint8_t PWM_A = 9;   // Confirmado en tu placa: NO es D6.
const uint8_t DIR_B = 4;
const uint8_t PWM_B = 5;
const uint8_t IR_PIN = A1; // Receptor en P4.

// Ajustar solo si la posicion o polaridad de las ruedas lo requiere.
const bool A_ES_IZQUIERDO = true;
const bool INVERTIR_A = true;
const bool INVERTIR_B = true;

const uint8_t BRILLO = 5;              // 0-15.
const unsigned long PARADA_MS = 300;  // Tiempo sin orden de movimiento.
const unsigned long PAUSA_CAMBIO_MS = 60;
int velocidad = 150;                 // * y # permiten ajustar 100-220.

Adafruit_8x8matrix pantalla;

// Comandos NEC de IRremote 4.x (NO son los antiguos valores FFxxxx).
const uint16_t K_ARRIBA = 0x46;
const uint16_t K_ABAJO = 0x15;
const uint16_t K_IZQUIERDA = 0x44;
const uint16_t K_DERECHA = 0x43;
const uint16_t K_OK = 0x40;
const uint16_t K_ASTERISCO = 0x42;
const uint16_t K_ALMOHADILLA = 0x4A;

// Indice del array = numero pulsado en el mando.
const uint8_t TECLAS_NUMERO[10] PROGMEM = {
  0x52, 0x16, 0x19, 0x0D, 0x0C,
  0x18, 0x5E, 0x08, 0x1C, 0x5A
};

const uint8_t CARAS[10][8] PROGMEM = {
  {0x00,0x66,0x66,0x00,0x00,0x3C,0x00,0x00}, // 0: neutra
  {0x00,0x66,0x66,0x00,0x42,0x24,0x18,0x00}, // 1: sonrisa
  {0x00,0x66,0x66,0x00,0x18,0x24,0x42,0x00}, // 2: triste
  {0x00,0x66,0x66,0x00,0x18,0x24,0x24,0x18}, // 3: sorpresa
  {0x00,0x60,0x6E,0x00,0x42,0x24,0x18,0x00}, // 4: guino
  {0x42,0x24,0x66,0x00,0x3C,0x42,0x00,0x00}, // 5: enfadada
  {0x00,0x00,0x66,0x00,0x18,0x00,0x00,0x00}, // 6: dormida
  {0xFF,0x7E,0x66,0x00,0x42,0x24,0x18,0x00}, // 7: gafas de sol
  {0x66,0xFF,0xFF,0xFF,0x7E,0x3C,0x18,0x00}, // 8: corazon
  {0xA5,0x5A,0xA5,0x00,0x42,0x24,0x18,0x00}  // 9: ojos de X
};

// Estados internos. No se hace ningun movimiento al arrancar.
const uint8_t PARADO = 0;
const uint8_t ADELANTE = 1;
const uint8_t ATRAS = 2;
const uint8_t IZQUIERDA = 3;
const uint8_t DERECHA = 4;
uint8_t movimiento = PARADO;
bool arranquePendiente = false;
unsigned long inicioCambio = 0;
unsigned long ultimaOrdenMovimiento = 0;

bool historialIRValido = false;
uint16_t ultimaTecla = 0;
unsigned long ultimoIR = 0;

// ================= MATRIZ =================
void mostrarCara(uint8_t numero) {
  if (numero > 9) return;
  pantalla.clear();
  for (uint8_t y = 0; y < 8; ++y) {
    uint8_t fila = pgm_read_byte(&CARAS[numero][y]);
    for (uint8_t x = 0; x < 8; ++x) {
      if (fila & (0x80 >> x)) {
        // Correccion que ya comprobaste: circular, sin perder columnas.
        pantalla.drawPixel((x + 1) % 8, y, LED_ON);
      }
    }
  }
  pantalla.writeDisplay();
}

// ================= MOTORES =================
void parar() {
  analogWrite(PWM_A, 0);
  analogWrite(PWM_B, 0);
  movimiento = PARADO;
  arranquePendiente = false;
}

void escribirCanal(uint8_t dir, uint8_t pwm, int v, bool invertir) {
  if (invertir) v = -v;
  if (v == 0) {
    analogWrite(pwm, 0);
    return;
  }
  digitalWrite(dir, v > 0 ? HIGH : LOW);
  analogWrite(pwm, abs(v));
}

void solicitarMovimiento(uint8_t nuevo) {
  ultimaOrdenMovimiento = millis();
  if (movimiento == nuevo) return; // Repeticion: mantener la marcha.

  // Quitar potencia antes de cambiar de sentido, sin bloquear el mando.
  parar();
  movimiento = nuevo;
  inicioCambio = millis();
  arranquePendiente = true;
}

void actualizarMotores() {
  if (movimiento == PARADO) return;
  unsigned long ahora = millis();

  if (ahora - ultimaOrdenMovimiento >= PARADA_MS) {
    parar();
    historialIRValido = false; // No rearrancar con una repeticion tardia.
    Serial.println(F("PARADA: mando soltado o sin senal"));
    return;
  }

  if (!arranquePendiente || ahora - inicioCambio < PAUSA_CAMBIO_MS) return;

  int izquierda = 0;
  int derecha = 0;
  switch (movimiento) {
    case ADELANTE:  izquierda =  velocidad; derecha =  velocidad; break;
    case ATRAS:     izquierda = -velocidad; derecha = -velocidad; break;
    case IZQUIERDA: izquierda = -velocidad; derecha =  velocidad; break;
    case DERECHA:   izquierda =  velocidad; derecha = -velocidad; break;
    default: parar(); return;
  }

  escribirCanal(DIR_A, PWM_A, A_ES_IZQUIERDO ? izquierda : derecha, INVERTIR_A);
  escribirCanal(DIR_B, PWM_B, A_ES_IZQUIERDO ? derecha : izquierda, INVERTIR_B);
  arranquePendiente = false;
}

// ================= MANDO =================
void procesarTecla(uint16_t tecla, bool repetida) {
  switch (tecla) {
    case K_ARRIBA:    solicitarMovimiento(ADELANTE);  return;
    case K_ABAJO:     solicitarMovimiento(ATRAS);     return;
    case K_IZQUIERDA: solicitarMovimiento(IZQUIERDA); return;
    case K_DERECHA:   solicitarMovimiento(DERECHA);   return;
  }

  // Cualquier otra tecla reconocida corta los motores antes de usar I2C.
  parar();
  if (repetida) return; // No repetir cambios de cara o velocidad.

  if (tecla == K_OK) {
    Serial.println(F("PARADA: OK"));
    return;
  }
  if (tecla == K_ASTERISCO || tecla == K_ALMOHADILLA) {
    velocidad += (tecla == K_ASTERISCO ? -20 : 20);
    velocidad = constrain(velocidad, 100, 220);
    Serial.print(F("Velocidad PWM: "));
    Serial.println(velocidad);
    return;
  }
  for (uint8_t n = 0; n < 10; ++n) {
    if (tecla == pgm_read_byte(&TECLAS_NUMERO[n])) {
      mostrarCara(n);
      Serial.print(F("Cara: "));
      Serial.println(n);
      return;
    }
  }
  Serial.println(F("Tecla sin asignar: motores detenidos"));
}

void leerMando() {
  if (!IrReceiver.decode()) return;

  // Copiar antes de permitir la recepcion de otra trama.
  const decode_type_t protocolo = IrReceiver.decodedIRData.protocol;
  const uint16_t direccion = IrReceiver.decodedIRData.address;
  const uint16_t tecla = IrReceiver.decodedIRData.command;
  const uint8_t flags = IrReceiver.decodedIRData.flags;
  const uint16_t bits = IrReceiver.decodedIRData.numberOfBits;
  IrReceiver.resume();

  if ((flags & (IRDATA_FLAGS_WAS_OVERFLOW | IRDATA_FLAGS_PARITY_FAILED)) ||
      (protocolo != NEC && protocolo != NEC2)) return;

  bool repetida = flags & IRDATA_FLAGS_IS_REPEAT;
  // Una trama completa con otra tecla es una orden nueva, incluso
  // si IRremote marca repeticion por haber pulsado muy deprisa.
  if (repetida && bits == 32 &&
      (!historialIRValido || tecla != ultimaTecla)) repetida = false;
  if (!repetida) {
    Serial.print(F("IR direccion=0x"));
    Serial.print(direccion, HEX);
    Serial.print(F(" comando=0x"));
    Serial.println(tecla, HEX);
  }

  // Direccion del mando Keyestudio de 17 botones.
  if (direccion != 0x0000) return;

  unsigned long ahora = millis();
  if (repetida) {
    // Las repeticiones NEC no son ordenes nuevas: exigir una previa reciente.
    if (!historialIRValido || tecla != ultimaTecla ||
        ahora - ultimoIR >= PARADA_MS) return;
  } else {
    ultimaTecla = tecla;
    historialIRValido = true;
  }
  ultimoIR = ahora;
  procesarTecla(tecla, repetida);
}

// ================= INICIO Y BUCLE =================
void setup() {
  pinMode(DIR_A, OUTPUT);
  pinMode(PWM_A, OUTPUT);
  pinMode(DIR_B, OUTPUT);
  pinMode(PWM_B, OUTPUT);
  parar();
  Serial.begin(9600);

  // Servo en D10 sin pulsos; no se utiliza Timer1 para controlarlo.
  pinMode(10, OUTPUT);
  digitalWrite(10, LOW);
  // Ultrasonidos conectado pero inactivo; D13 queda como entrada.
  pinMode(12, OUTPUT);
  digitalWrite(12, LOW);
  pinMode(13, INPUT);

  Wire.begin();
  if (!pantalla.begin(0x70)) {
    Serial.println(F("ERROR: matriz no detectada en 0x70. Revisar A4/A5, 5V y G."));
    Serial.println(F("Motores desactivados. Corregir conexion y reiniciar."));
    while (true) { delay(100); }
  }
  pantalla.setBrightness(BRILLO);
  pantalla.setRotation(0);
  mostrarCara(1);

  // Sin parpadeo de LED: no convertir D13 (Echo) en una salida.
  IrReceiver.begin(IR_PIN, DISABLE_LED_FEEDBACK);
  Serial.println(F("TURTLE V3: mando IR + caras. Listo, motores parados."));
  Serial.println(F("Flechas mantenidas=mover, OK=parar, 0-9=caras."));
  Serial.println(F("*=menos velocidad, #=mas velocidad. Monitor a 9600."));
}

void loop() {
  actualizarMotores();
  leerMando();
  actualizarMotores();
}
