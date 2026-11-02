/*
  KEYESTUDIO KS0464 - MANDO + CARAS + AUTOMATICO, V4.1 - P2 / ECHO D13
  Para Arduino UNO / ATmega328P de 16 MHz.

  Se conserva la configuracion COMPROBADA de tu shield:
    Motor A: DIR D2, PWM D9. Motor B: DIR D4, PWM D5.
    Servo: D10. Ultrasonidos: TRIG D12, ECHO D13.
    Receptor IR: A1 (P4). Matriz HT16K33: A4/A5, 0x70.

  Librerias: IRremote 4.x, Adafruit LED Backpack, Adafruit GFX, BusIO.
  NO instalar ni incluir Servo.h, ServoTimer2 o NewPing para este programa.

  Mando de 17 botones:
    #       : alternar MANUAL / AUTO. Al salir se detiene.
    OK      : detener y volver a MANUAL desde cualquier fase.
    Flechas : conducir en MANUAL mientras se mantienen pulsadas.
    0-9     : detener y cambiar cara en MANUAL.
    *       : detener y alternar velocidad MANUAL 120/150/180/210.
    En AUTO se ignoran flechas, numeros y *; la cara es siempre la 5.

  AUTO:
    Comprueba el frente antes de arrancar. Avanza y mide continuamente.
    Ante obstaculo o lectura invalida, para, mira izquierda y derecha,
    elige el lado con mas espacio, centra el cuello y gira un paso.
    Para y vuelve a medir antes de seguir; repite si hace falta.
    Si no hay paso fiable, espera parado y vuelve a comprobar.
    NO retrocede a ciegas. Sin eco NO equivale a camino libre.

  CORRECCION V4.1:
    El ultrasonico sigue en P2: G, V, D12/TRIG, D13/ECHO.
    Se elimina el codigo de LED de IRremote para no ocupar D13.
    Se reafirma D13 como INPUT despues de inicializar el receptor IR.
    Monitor serie a 9600: lecturas SONAR durante AUTO, sin bloquear.
    SIN_ECO / ECO_ALTO no autorizan el avance. No se cambian los umbrales.

  TEMPORIZADORES:
    Timer1 conserva el PWM del motor A en D9.
    Timer2 lo utiliza IRremote. No usar tone() ni PWM D3/D11.
    El servo se controla por pulsos software, sin apropiarse de un timer.
    ECHO D13 se mide por interrupcion PCINT0, sin pulseIn bloqueante.
    No se cambia la configuracion de Timer0 (millis/micros/PWM D5).

  PRIMERA PRUEBA: ruedas levantadas, baterias encendidas.
  Comprobar avance y lados del cuello ANTES de probar AUTO en el suelo.
  Usar solo bajo supervision, lejos de escaleras y bordes.
  Los giros son temporizados: ajustar GIRO_PASO_MS al robot real.

  Referencias:
    https://wiki.keyestudio.com/KS0298_Keyestudio_quick_connectors_Motor_Drive_Shield
    https://docs.keyestudio.com/projects/KS0464/en/latest/KS0464_arduino.html
    https://github.com/Arduino-IRremote/Arduino-IRremote
    https://github.com/adafruit/Adafruit_LED_Backpack
*/

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_LEDBackpack.h>
#include <avr/interrupt.h>
#include <util/atomic.h>
#include <string.h>

#if !defined(__AVR_ATmega328P__) || F_CPU != 16000000UL
#error "Selecciona Arduino Uno (ATmega328P a 16 MHz)."
#endif

#define DECODE_NEC
#define IR_USE_AVR_TIMER2
#define RAW_BUFFER_LENGTH 100
// Debe estar ANTES del include. DISABLE_LED_FEEDBACK por si solo no
// impide que algunas versiones de IRremote configuren LED_BUILTIN/D13.
#define NO_LED_FEEDBACK_CODE
#include <IRremote.hpp>

// ================= AJUSTES DEL ROBOT =================
const uint8_t DIR_A = 2;
const uint8_t PWM_A = 9;  // IMPORTANTE: tu shield usa D9, NO D6.
const uint8_t DIR_B = 4;
const uint8_t PWM_B = 5;
const uint8_t IR_PIN = A1;
const uint8_t SERVO_PIN = 10;
const uint8_t TRIG_PIN = 12;
const uint8_t ECHO_PIN = 13; // P2, PCINT5/PB5: no cambiar sin adaptar la ISR.
const bool DIAGNOSTICO_SONAR = true; // Monitor serie a 9600 baudios.

// Copiar aqui los ajustes de tu version manual si los habias cambiado.
const bool A_ES_IZQUIERDO = true;
const bool INVERTIR_A = true;
const bool INVERTIR_B = true;

// Izquierda/derecha se entienden desde el robot, mirando hacia delante.
const bool INVERTIR_CUELLO = false;
const uint8_t CUELLO_CENTRO = 84;
const uint8_t CUELLO_IZQUIERDA = 150;
const uint8_t CUELLO_DERECHA = 30;
// No llegar a 0/180. Reducir el recorrido si el soporte toca algo.
const uint16_t ASENTAR_CUELLO_MS = 650;

const uint8_t VELOCIDAD_AUTO = 140;
const uint8_t VELOCIDAD_GIRO = 160;
const int16_t DISTANCIA_PARADA_CM = 25;
const int16_t DISTANCIA_REANUDAR_CM = 35;
const int16_t DISTANCIA_LATERAL_CM = 35;
const uint16_t GIRO_PASO_MS = 400; // Aproximado; calibrar en el suelo.
const uint16_t ESPERA_BLOQUEADO_MS = 1500;
const uint8_t MAX_GIROS_SIN_AVANZAR = 6;

const uint8_t BRILLO = 5;
const uint16_t PARADA_MANUAL_MS = 300;
const uint16_t PAUSA_CAMBIO_MS = 60;
const uint32_t INTERVALO_SONAR_US = 70000UL;
const uint32_t TIMEOUT_ECO_US = 25000UL;
const uint16_t MAX_EDAD_LECTURA_MS = 300;
const uint16_t PERIODO_SERVO_US = 20000;

int velocidadManual = 150;
bool modoAuto = false;
bool falloPantalla = false;
uint8_t caraManual = 1;
uint8_t caraPendiente = 1;
bool actualizarCara = true;
Adafruit_8x8matrix pantalla;

const uint16_t K_ARRIBA = 0x46;
const uint16_t K_ABAJO = 0x15;
const uint16_t K_IZQUIERDA = 0x44;
const uint16_t K_DERECHA = 0x43;
const uint16_t K_OK = 0x40;
const uint16_t K_ASTERISCO = 0x42;
const uint16_t K_ALMOHADILLA = 0x4A;

const uint8_t TECLAS_NUMERO[10] PROGMEM = {
  0x52,0x16,0x19,0x0D,0x0C,0x18,0x5E,0x08,0x1C,0x5A
};
const uint8_t CARAS[10][8] PROGMEM = {
  {0x00,0x66,0x66,0x00,0x00,0x3C,0x00,0x00}, // 0 neutra
  {0x00,0x66,0x66,0x00,0x42,0x24,0x18,0x00}, // 1 sonrisa
  {0x00,0x66,0x66,0x00,0x18,0x24,0x42,0x00}, // 2 triste
  {0x00,0x66,0x66,0x00,0x18,0x24,0x24,0x18}, // 3 sorpresa
  {0x00,0x60,0x6E,0x00,0x42,0x24,0x18,0x00}, // 4 guino
  {0x42,0x24,0x66,0x00,0x3C,0x42,0x00,0x00}, // 5 MALO / AUTO
  {0x00,0x00,0x66,0x00,0x18,0x00,0x00,0x00}, // 6 dormida
  {0xFF,0x7E,0x66,0x00,0x42,0x24,0x18,0x00}, // 7 gafas
  {0x66,0xFF,0xFF,0xFF,0x7E,0x3C,0x18,0x00}, // 8 corazon
  {0xA5,0x5A,0xA5,0x00,0x42,0x24,0x18,0x00}  // 9 ojos X
};

const uint8_t PARADO=0, ADELANTE=1, ATRAS=2, IZQUIERDA=3, DERECHA=4;
uint8_t movimiento = PARADO;
uint8_t potenciaSolicitada = 0;
bool arranquePendiente = false;
uint32_t inicioCambio = 0;
uint32_t inicioMarcha = 0;
uint32_t ultimaOrdenMovimiento = 0;

bool historialIRValido = false;
uint16_t ultimaTecla = 0;
uint32_t ultimoIR = 0;

// Servo cooperativo: durante el pulso alto loop() solo atiende el pulso.
// Las interrupciones de IR, sonar y reloj siguen habilitadas.
bool servoAlto = false;
uint16_t servoAnchoUs = 1500;
uint32_t servoInicioUs = 0;

// Variables compartidas con la interrupcion del eco.
volatile bool ecoArmado = false;
volatile bool ecoSubio = false;
volatile bool ecoCompleto = false;
volatile uint32_t ecoInicioUs = 0;
volatile uint32_t ecoDuracionUs = 0;
bool sonarActivo = false;
bool sonarListo = false;
int16_t sonarCm = -1;
uint32_t sonarInicioUs = 0;
uint32_t ultimoDisparoUs = 0;
uint32_t ultimoLogSonarMs = 0;

const uint8_t AUTO_CENTRAR=0, AUTO_FRENTE=1, AUTO_AVANZAR=2,
  AUTO_ESPERAR_IZQ=3, AUTO_MEDIR_IZQ=4, AUTO_ESPERAR_DER=5,
  AUTO_MEDIR_DER=6, AUTO_CENTRAR_GIRO=7, AUTO_GIRAR=8,
  AUTO_BLOQUEADO=9;
uint8_t estadoAuto = AUTO_CENTRAR;
uint32_t inicioEstado = 0;
uint32_t ultimaLecturaFrontal = 0;
int16_t distanciaIzquierda = -1;
int16_t distanciaDerecha = -1;
uint8_t giroElegido = PARADO;
uint8_t ultimoGiro = DERECHA;
uint8_t girosSinAvanzar = 0;
uint8_t muestrasGrupo = 0;
uint8_t validasGrupo = 0;
int16_t minimoGrupo = 401;

// ================= REGISTRO SIN ESPERAS LARGAS =================
void registrar(const __FlashStringHelper *texto) {
  // No bloquear el bucle esperando a vaciar el puerto serie.
  size_t n = strlen_P(reinterpret_cast<PGM_P>(texto));
  if (Serial.availableForWrite() >= static_cast<int>(n + 2)) {
    Serial.println(texto);
  }
}

// Una linea breve como maximo cada 250 ms, solo si cabe en el buffer.
// No se esperan transmisiones serie, para no retrasar el servo ni el mando.
void registrarSonar(int16_t cm, const __FlashStringHelper *detalle) {
  if (!DIAGNOSTICO_SONAR) return;
  uint32_t ahora = millis();
  if (ahora - ultimoLogSonarMs < 250 || Serial.availableForWrite() < 54) return;
  ultimoLogSonarMs = ahora;
  Serial.print(F("SONAR "));
  if (estadoAuto == AUTO_MEDIR_IZQ) Serial.print(F("IZQ: "));
  else if (estadoAuto == AUTO_MEDIR_DER) Serial.print(F("DER: "));
  else Serial.print(F("FRENTE: "));
  if (cm >= 2) {
    Serial.print(cm);
    Serial.println(F(" cm"));
  } else {
    Serial.println(detalle);
  }
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
  if (v == 0) { analogWrite(pwm, 0); return; }
  // Se conserva el sentido utilizado en el programa manual anterior.
  digitalWrite(dir, v > 0 ? HIGH : LOW);
  analogWrite(pwm, abs(v));
}

void solicitarMovimiento(uint8_t nuevo, uint8_t potencia) {
  if (falloPantalla) return;
  ultimaOrdenMovimiento = millis();
  if (movimiento == nuevo && potenciaSolicitada == potencia) return;
  parar();
  if (nuevo == PARADO) return;
  movimiento = nuevo;
  potenciaSolicitada = potencia;
  inicioCambio = millis();
  arranquePendiente = true;
}

void actualizarMotores() {
  if (movimiento == PARADO) return;
  uint32_t ahora = millis();
  if (!modoAuto && ahora - ultimaOrdenMovimiento >= PARADA_MANUAL_MS) {
    parar();
    historialIRValido = false;
    return;
  }
  if (!arranquePendiente || ahora - inicioCambio < PAUSA_CAMBIO_MS) return;
  int izquierda = 0, derecha = 0;
  switch (movimiento) {
    case ADELANTE:  izquierda = potenciaSolicitada; derecha = potenciaSolicitada; break;
    case ATRAS:     izquierda = -potenciaSolicitada; derecha = -potenciaSolicitada; break;
    case IZQUIERDA: izquierda = -potenciaSolicitada; derecha = potenciaSolicitada; break;
    case DERECHA:   izquierda = potenciaSolicitada; derecha = -potenciaSolicitada; break;
    default: parar(); return;
  }
  escribirCanal(DIR_A, PWM_A, A_ES_IZQUIERDO ? izquierda : derecha, INVERTIR_A);
  escribirCanal(DIR_B, PWM_B, A_ES_IZQUIERDO ? derecha : izquierda, INVERTIR_B);
  inicioMarcha = ahora;
  arranquePendiente = false;
}

// ================= CUELLO SIN Servo.h =================
void orientarCuello(uint8_t grados) {
  grados = constrain(grados, 30, 150);
  if (INVERTIR_CUELLO) grados = 180 - grados;
  servoAnchoUs = 500U + (static_cast<uint32_t>(grados) * 2000U) / 180U;
}

void actualizarServo() {
  uint32_t ahora = micros();
  if (servoAlto) {
    if (ahora - servoInicioUs >= servoAnchoUs) {
      digitalWrite(SERVO_PIN, LOW);
      servoAlto = false;
    }
  } else if (ahora - servoInicioUs >= PERIODO_SERVO_US) {
    digitalWrite(SERVO_PIN, HIGH);
    servoInicioUs = micros();
    servoAlto = true;
  }
}

// ================= ULTRASONIDOS ASINCRONO =================
ISR(PCINT0_vect) {
  if (!ecoArmado) return;
  if (PINB & _BV(PB5)) { // D13 alto: principio del eco.
    if (!ecoSubio) {
      ecoInicioUs = micros();
      ecoSubio = true;
    }
  } else if (ecoSubio) {
    ecoDuracionUs = micros() - ecoInicioUs;
    ecoCompleto = true;
    ecoArmado = false;
  }
}

void cancelarSonar() {
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    ecoArmado = false;
    ecoSubio = false;
    ecoCompleto = false;
  }
  sonarActivo = false;
  sonarListo = false;
  digitalWrite(TRIG_PIN, LOW);
}

void iniciarMedida() {
  if (sonarActivo || sonarListo) return;
  uint32_t ahora = micros();
  if (ahora - ultimoDisparoUs < INTERVALO_SONAR_US) return;
  ultimoDisparoUs = ahora;
  if (digitalRead(ECHO_PIN) == HIGH) {
    // Eco atascado o aun activo: NO interpretar como camino libre.
    sonarCm = -1;
    sonarListo = true;
    registrarSonar(-1, F("ECO_ALTO"));
    return;
  }
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    ecoSubio = false;
    ecoCompleto = false;
    ecoArmado = true;
  }
  sonarActivo = true;
  sonarInicioUs = micros();
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(12); // Unicamente el pulso de disparo, no espera al eco.
  digitalWrite(TRIG_PIN, LOW);
}

void actualizarSonar() {
  if (!sonarActivo) return;
  bool completo;
  uint32_t duracion;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    completo = ecoCompleto;
    duracion = ecoDuracionUs;
  }
  if (completo) {
    int16_t cm = -1;
    if (duracion <= TIMEOUT_ECO_US) cm = static_cast<int16_t>((duracion + 29UL) / 58UL);
    sonarCm = (cm >= 2 && cm <= 400) ? cm : -1;
    sonarActivo = false;
    sonarListo = true;
    registrarSonar(sonarCm, F("ECO_FUERA_RANGO"));
  } else if (micros() - sonarInicioUs >= TIMEOUT_ECO_US) {
    cancelarSonar();
    sonarCm = -1;
    sonarListo = true;
    registrarSonar(-1, F("SIN_ECO"));
  }
}

void reiniciarGrupo() {
  cancelarSonar();
  muestrasGrupo = 0;
  validasGrupo = 0;
  minimoGrupo = 401;
}

bool medirGrupo(int16_t &resultado) {
  if (sonarListo) {
    sonarListo = false;
    ++muestrasGrupo;
    if (sonarCm >= 2) {
      ++validasGrupo;
      if (sonarCm < minimoGrupo) minimoGrupo = sonarCm;
    }
    if (muestrasGrupo >= 3) {
      // Exigir al menos 2 ecos validos. Usar el mas cercano, no el mas lejano.
      resultado = validasGrupo >= 2 ? minimoGrupo : -1;
      return true;
    }
  }
  iniciarMedida();
  return false;
}

// ================= AUTOMATICO =================
void cambiarEstado(uint8_t nuevo) {
  estadoAuto = nuevo;
  inicioEstado = millis();
}

void activarAutomatico() {
  parar();
  cancelarSonar();
  modoAuto = true;
  girosSinAvanzar = 0;
  orientarCuello(CUELLO_CENTRO);
  caraPendiente = 5;
  actualizarCara = true;
  cambiarEstado(AUTO_CENTRAR);
  registrar(F("AUTO: cara de malo; comprobando frente"));
}

void volverAManual() {
  parar();
  cancelarSonar();
  modoAuto = false;
  orientarCuello(CUELLO_CENTRO);
  caraPendiente = caraManual;
  actualizarCara = true;
  registrar(F("MANUAL: detenido"));
}

void explorarLados() {
  parar();
  cancelarSonar();
  distanciaIzquierda = distanciaDerecha = -1;
  orientarCuello(CUELLO_IZQUIERDA);
  cambiarEstado(AUTO_ESPERAR_IZQ);
  registrar(F("AUTO: parado; mirando ambos lados"));
}

void elegirGiro() {
  bool izquierdaLibre = distanciaIzquierda >= DISTANCIA_LATERAL_CM;
  bool derechaLibre = distanciaDerecha >= DISTANCIA_LATERAL_CM;
  giroElegido = PARADO;
  if (girosSinAvanzar < MAX_GIROS_SIN_AVANZAR) {
    if (izquierdaLibre && derechaLibre) {
      if (abs(distanciaIzquierda - distanciaDerecha) <= 8) {
        giroElegido = ultimoGiro == IZQUIERDA ? DERECHA : IZQUIERDA;
      } else {
        giroElegido = distanciaIzquierda > distanciaDerecha ? IZQUIERDA : DERECHA;
      }
    } else if (izquierdaLibre) giroElegido = IZQUIERDA;
    else if (derechaLibre) giroElegido = DERECHA;
  }
  if (Serial.availableForWrite() >= 48) {
    Serial.print(F("Lados cm: I=")); Serial.print(distanciaIzquierda);
    Serial.print(F(" D=")); Serial.println(distanciaDerecha);
  }
  orientarCuello(CUELLO_CENTRO);
  cancelarSonar();
  cambiarEstado(AUTO_CENTRAR_GIRO);
}

void actualizarAutomatico() {
  if (!modoAuto || falloPantalla) return;
  uint32_t ahora = millis();
  int16_t frente;
  switch (estadoAuto) {
    case AUTO_CENTRAR:
      if (ahora - inicioEstado >= ASENTAR_CUELLO_MS) {
        reiniciarGrupo(); cambiarEstado(AUTO_FRENTE);
      }
      break;

    case AUTO_FRENTE:
      if (medirGrupo(frente)) {
        if (frente >= DISTANCIA_REANUDAR_CM) {
          ultimaLecturaFrontal = ahora;
          solicitarMovimiento(ADELANTE, VELOCIDAD_AUTO);
          cambiarEstado(AUTO_AVANZAR);
          registrar(F("AUTO: frente libre; avanzando"));
        } else explorarLados();
      }
      break;

    case AUTO_AVANZAR:
      if (sonarListo) {
        sonarListo = false;
        if (sonarCm < 0 || sonarCm <= DISTANCIA_PARADA_CM) {
          explorarLados(); break;
        }
        ultimaLecturaFrontal = ahora;
      }
      if (ahora - ultimaLecturaFrontal > MAX_EDAD_LECTURA_MS) {
        explorarLados(); break;
      }
      // Reiniciar el contador solo tras avanzar un tiempo, no por un eco aislado.
      if (!arranquePendiente && ahora - inicioMarcha >= 1000) girosSinAvanzar = 0;
      iniciarMedida();
      break;

    case AUTO_ESPERAR_IZQ:
      if (ahora - inicioEstado >= ASENTAR_CUELLO_MS) {
        reiniciarGrupo(); cambiarEstado(AUTO_MEDIR_IZQ);
      }
      break;
    case AUTO_MEDIR_IZQ:
      if (medirGrupo(distanciaIzquierda)) {
        orientarCuello(CUELLO_DERECHA);
        cancelarSonar(); cambiarEstado(AUTO_ESPERAR_DER);
      }
      break;
    case AUTO_ESPERAR_DER:
      if (ahora - inicioEstado >= ASENTAR_CUELLO_MS) {
        reiniciarGrupo(); cambiarEstado(AUTO_MEDIR_DER);
      }
      break;
    case AUTO_MEDIR_DER:
      if (medirGrupo(distanciaDerecha)) elegirGiro();
      break;

    case AUTO_CENTRAR_GIRO:
      if (ahora - inicioEstado >= ASENTAR_CUELLO_MS) {
        if (giroElegido == PARADO) {
          parar(); cambiarEstado(AUTO_BLOQUEADO);
          registrar(F("AUTO: sin paso fiable; esperando parado"));
        } else {
          ultimoGiro = giroElegido;
          ++girosSinAvanzar;
          solicitarMovimiento(giroElegido, VELOCIDAD_GIRO);
          cambiarEstado(AUTO_GIRAR);
          registrar(giroElegido == IZQUIERDA ? F("AUTO: giro izquierda") : F("AUTO: giro derecha"));
        }
      }
      break;

    case AUTO_GIRAR:
      // Tiempo contado desde el encendido real, sin incluir la pausa de inversion.
      if (!arranquePendiente && ahora - inicioMarcha >= GIRO_PASO_MS) {
        parar(); cancelarSonar(); cambiarEstado(AUTO_CENTRAR);
      }
      break;

    case AUTO_BLOQUEADO:
      if (ahora - inicioEstado >= ESPERA_BLOQUEADO_MS) {
        reiniciarGrupo(); cambiarEstado(AUTO_FRENTE);
      }
      break;
    default: volverAManual(); break;
  }
}

// ================= MANDO =================
void procesarTecla(uint16_t tecla, bool repetida) {
  if (falloPantalla) { parar(); return; }
  if (tecla == K_OK) { volverAManual(); return; }
  if (tecla == K_ALMOHADILLA) {
    if (!repetida) {
      if (modoAuto) volverAManual();
      else activarAutomatico();
    }
    return;
  }
  if (modoAuto) return; // No quitar la cara de malo ni mezclar ordenes.

  switch (tecla) {
    case K_ARRIBA:    solicitarMovimiento(ADELANTE, velocidadManual); return;
    case K_ABAJO:     solicitarMovimiento(ATRAS, velocidadManual); return;
    case K_IZQUIERDA: solicitarMovimiento(IZQUIERDA, velocidadManual); return;
    case K_DERECHA:   solicitarMovimiento(DERECHA, velocidadManual); return;
  }
  parar();
  if (repetida) return;
  if (tecla == K_ASTERISCO) {
    velocidadManual = velocidadManual >= 210 ? 120 : velocidadManual + 30;
    if (Serial.availableForWrite() >= 32) {
      Serial.print(F("PWM manual: ")); Serial.println(velocidadManual);
    }
    return;
  }
  for (uint8_t n=0; n<10; ++n) {
    if (tecla == pgm_read_byte(&TECLAS_NUMERO[n])) {
      caraManual = caraPendiente = n;
      actualizarCara = true;
      return;
    }
  }
}

void leerMando() {
  if (!IrReceiver.decode()) return;
  const decode_type_t protocolo = IrReceiver.decodedIRData.protocol;
  const uint16_t direccion = IrReceiver.decodedIRData.address;
  const uint16_t tecla = IrReceiver.decodedIRData.command;
  const uint8_t flags = IrReceiver.decodedIRData.flags;
  const uint16_t bits = IrReceiver.decodedIRData.numberOfBits;
  IrReceiver.resume();
  if ((flags & (IRDATA_FLAGS_WAS_OVERFLOW | IRDATA_FLAGS_PARITY_FAILED)) ||
      (protocolo != NEC && protocolo != NEC2) || direccion != 0x0000) return;

  uint32_t ahora = millis();
  bool repetida = flags & IRDATA_FLAGS_IS_REPEAT;
  if (repetida && bits == 32 &&
      (!historialIRValido || tecla != ultimaTecla || ahora - ultimoIR >= PARADA_MANUAL_MS)) {
    repetida = false;
  }
  // Algunos mandos repiten la trama completa. No alternar AUTO por cada trama.
  if (!repetida && historialIRValido && tecla == ultimaTecla && ahora - ultimoIR < 200) {
    repetida = true;
  }
  if (repetida) {
    if (!historialIRValido || tecla != ultimaTecla ||
        ahora - ultimoIR >= PARADA_MANUAL_MS) return;
  } else {
    ultimaTecla = tecla;
    historialIRValido = true;
  }
  ultimoIR = ahora;
  procesarTecla(tecla, repetida);
}

// ================= MATRIZ =================
void refrescarCara() {
  if (!actualizarCara || falloPantalla) return;
  actualizarCara = false;
  pantalla.clear();
  for (uint8_t y=0; y<8; ++y) {
    uint8_t fila = pgm_read_byte(&CARAS[caraPendiente][y]);
    for (uint8_t x=0; x<8; ++x) {
      if (fila & (0x80 >> x)) pantalla.drawPixel((x+1)%8, y, LED_ON);
    }
  }
  pantalla.writeDisplay();
#if defined(WIRE_HAS_TIMEOUT)
  if (Wire.getWireTimeoutFlag()) {
    Wire.clearWireTimeoutFlag();
    parar(); cancelarSonar(); modoAuto = false; falloPantalla = true;
    orientarCuello(CUELLO_CENTRO);
    registrar(F("ERROR I2C: detenido. Revisar matriz y reiniciar."));
  }
#endif
}

// ================= SETUP / LOOP =================
void setup() {
  pinMode(DIR_A, OUTPUT); pinMode(PWM_A, OUTPUT);
  pinMode(DIR_B, OUTPUT); pinMode(PWM_B, OUTPUT);
  parar();
  Serial.begin(9600);
  pinMode(SERVO_PIN, OUTPUT); digitalWrite(SERVO_PIN, LOW);
  orientarCuello(CUELLO_CENTRO);
  pinMode(TRIG_PIN, OUTPUT); digitalWrite(TRIG_PIN, LOW);
  pinMode(ECHO_PIN, INPUT);
  cancelarSonar();
  ultimoDisparoUs = micros() - INTERVALO_SONAR_US;

  // Habilitar exclusivamente PB5/D13 como origen de PCINT0.
  // No anadir otra libreria que defina tambien ISR(PCINT0_vect).
  PCMSK0 = _BV(PCINT5);
  PCIFR = _BV(PCIF0);
  PCICR |= _BV(PCIE0);

  Wire.begin();
#if defined(WIRE_HAS_TIMEOUT)
  Wire.setWireTimeout(3000UL, true);
#endif
  if (!pantalla.begin(0x70)) {
    registrar(F("ERROR: matriz no detectada; motores bloqueados"));
    falloPantalla = true;
  } else {
    pantalla.setBrightness(BRILLO);
    pantalla.setRotation(0);
    refrescarCara();
  }
  // El macro NO_LED_FEEDBACK_CODE excluye el LED de recepcion/transmision.
  IrReceiver.begin(IR_PIN, DISABLE_LED_FEEDBACK);
  // Defensa adicional: ECHO es una ENTRADA, tambien despues de begin().
  // No usar digitalWrite(13,HIGH), LED_BUILTIN ni SPI en este sketch.
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(ECHO_PIN, LOW); // Sin resistencia pull-up interna.
  servoInicioUs = micros() - PERIODO_SERVO_US;
  registrar(F("TURTLE V4.1 P2: #=AUTO/MANUAL; OK=parar"));
  // Mensajes iniciales: el robot aun esta parado y no ha iniciado pulsos.
  Serial.println(F("P2: TRIG=D12, ECHO=D13. IR sin LED D13."));
  Serial.println(F("AUTO: inicia con >=35 cm validos; sin eco no avanza."));
}

void loop() {
  actualizarServo();
  // Nunca iniciar una transferencia I2C ni otra tarea larga con el pulso alto.
  // El bloqueo cooperativo dura como maximo el ancho del pulso (~2.2 ms),
  // mientras las interrupciones siguen recibiendo IR y midiendo el eco.
  if (servoAlto) return;
  actualizarSonar();
  leerMando();                       // Prioridad: poder cancelar cualquier fase.
  actualizarAutomatico();
  actualizarMotores();
  refrescarCara();
}
