# Keyestudio Turtle — Mando, caras y modo automático

Demo para el robot **Keyestudio KS0464 con Arduino UNO / ATmega328P a 16 MHz**. Permite conducir con un mando infrarrojo, seleccionar diez expresiones en una matriz LED y activar un modo de exploración con el sensor ultrasónico montado sobre el servo.

**`#` alterna manual/automático. `OK` detiene el robot y vuelve a manual. En automático se muestra la cara de malo.**

> Para el montaje con el ultrasónico en **P2**, utilizar como punto de partida **V4.1 / Auto_P2**. Incluye protección del pin de eco D13 y diagnóstico serie. La corrección está implementada, pero su funcionamiento completo en el robot todavía debe verificarse.

## Versiones incluidas

| Versión | Sketch | Qué incluye |
|---|---|---|
| **V4.0** | [Turtle_Mando_Caras_Auto.ino](Turtle_Mando_Caras_Auto/Turtle_Mando_Caras_Auto.ino) | Conducción manual, diez caras, exploración automática, servo por software y lectura del eco por interrupción. |
| **V4.1 — P2** | [Turtle_Mando_Caras_Auto_P2.ino](Turtle_Mando_Caras_Auto_P2/Turtle_Mando_Caras_Auto_P2.ino) | Todo lo anterior, exclusión del código de LED de IRremote, reconfiguración de D13 como entrada y mensajes de diagnóstico del ultrasónico. |

**P2 no es una nueva conexión de V4.1:** ambas versiones utilizan TRIG en D12 y ECHO en D13. Tampoco cambian los pines de los motores, las caras, los controles ni los umbrales de distancia.

### Estructura del repositorio

```text
.
├── README.md
├── Turtle_Mando_Caras_Auto/
│   └── Turtle_Mando_Caras_Auto.ino
└── Turtle_Mando_Caras_Auto_P2/
    └── Turtle_Mando_Caras_Auto_P2.ino
```

Cada versión es un programa independiente. Mantenerlas en carpetas distintas, con el mismo nombre que su archivo principal, siguiendo la [estructura de sketches de Arduino][sketch-spec]. **No cargar ambas como pestañas del mismo sketch:** duplicarían `setup()`, `loop()` y el resto de las definiciones.

## Hardware y conexiones

La asignación siguiente corresponde al montaje comprobado durante el desarrollo. No debe sustituirse por la de otra revisión del shield únicamente porque el robot se comercialice con el mismo nombre.

| Componente | Conexión en este montaje | Pines usados por el código |
|---|---|---|
| Motor A | Salida de motor **A o A1**, de dos pines | Dirección **D2**; PWM **D9** |
| Motor B | Salida de motor **B o B1**, de dos pines | Dirección **D4**; PWM **D5** |
| Servo del cuello | Fila **~10**, respetando S / 5V / G | Señal **D10** |
| Sensor ultrasónico | **P2**, marcado G / V / D12 / D13 | GND / 5V / TRIG **D12** / ECHO **D13** |
| Receptor infrarrojo | **P4**, marcado G / V / A1 | GND / 5V / señal **A1** |
| Matriz LED HT16K33 | Pines G / 5V / A4 / A5 | GND / 5V / SDA **A4** / SCL **A5**; dirección **0x70** |
| Alimentación de motores | Baterías del kit en **BAT** | Interruptor del shield encendido |

**El PWM del motor A es D9, no D6.** Esta asignación hizo funcionar ambos motores en el robot y coincide con el ejemplo del [shield KS0298][shield]. A/A1 comparten un canal; B/B1 comparten el otro. Para conducir cada rueda por separado, conectar una al grupo A y otra al grupo B.

**No confundir el A1 del receptor IR con la salida de motor A1:** son conectores y funciones diferentes. Las etiquetas de la tabla indican señales, no una orientación izquierda/derecha válida desde cualquier ángulo de la placa.

Desconectar **USB y baterías antes de cambiar conexiones**. Para probar los motores, utilizar las baterías del kit; no depender solo del USB.

## Dependencias y carga

| Dependencia | Uso |
|---|---|
| **Arduino AVR Boards** | Soporte para Arduino UNO / ATmega328P a 16 MHz. |
| **IRremote 4.x**, de Armin Joachimsmeyer | Recepción del mando mediante `IRremote.hpp`. |
| **Adafruit LED Backpack Library** | Control de la matriz HT16K33. |
| **Adafruit GFX Library** | Operaciones de dibujo. |
| **Adafruit BusIO** | Dependencia de las bibliotecas Adafruit. |
| `Wire` y cabeceras AVR | Proporcionadas por el entorno/core correspondiente. |

El proyecto utiliza la [API moderna de IRremote][irremote], no el código antiguo basado en `IRremote.h`. No necesita `Matrix.h`, `Servo.h`, `ServoTimer2` ni `NewPing`. Tener esas bibliotecas instaladas no es un problema; **no incorporarlas al sketch sin revisar los conflictos de temporizadores e interrupciones**.

1. Instalar las bibliotecas desde el gestor de bibliotecas de Arduino IDE y aceptar sus dependencias.
2. Abrir **solo** `Turtle_Mando_Caras_Auto_P2/Turtle_Mando_Caras_Auto_P2.ino` para utilizar V4.1.
3. Seleccionar **Arduino Uno** y el puerto serie correspondiente.
4. Revisar los ajustes de sentido de motores y cuello descritos más abajo. Verificar y subir el programa.
5. Abrir el monitor serie a **9600 baudios** y comenzar con las ruedas levantadas y las baterías encendidas.

El código comprueba la arquitectura y la frecuencia de CPU. No es directamente portable a UNO R4, ESP32 u otras placas: utiliza registros e interrupciones específicos del ATmega328P.

## Controles del mando

El programa espera los códigos del mando de 17 botones del kit. Arranca **parado, en manual, con sonrisa y el cuello centrado**.

| Botón | Modo manual | Modo automático |
|---|---|---|
| **↑** | Avanzar mientras se mantiene pulsado. | Se ignora. |
| **↓** | Retroceder mientras se mantiene pulsado. | Se ignora. |
| **← / →** | Girar sobre su eje mientras se mantiene pulsado. | Se ignora. |
| **OK** | Detener y centrar el cuello. | Detener, centrar el cuello y volver a manual. |
| **#** | Entrar en automático tras detenerse. | Detenerse y volver a manual. |
| **0–9** | Detenerse y seleccionar una cara. | Se ignoran; se conserva la cara de malo. |
| **\*** | Detenerse y cambiar la velocidad manual. | Se ignora. |

En manual se corta la potencia tras **300 ms sin una orden de movimiento válida**. El programa filtra las repeticiones del mando para que mantener `#` pulsado no alterne continuamente entre modos.

La velocidad manual inicial es **150**. Las pulsaciones de `*` recorren **150 → 180 → 210 → 120 → 150…**, en valores PWM de 0 a 255. No modifican la velocidad del modo automático.

Al salir de automático se recupera la última cara seleccionada en manual.

### Expresiones

| Número | Cara | Número | Cara |
|---|---|---|---|
| **0** | Neutra | **5** | Malo / enfadado — utilizada en automático |
| **1** | Sonrisa | **6** | Dormida |
| **2** | Triste | **7** | Gafas de sol |
| **3** | Sorpresa | **8** | Corazón |
| **4** | Guiño | **9** | Ojos de X |

Los dibujos están en `CARAS[10][8]`, almacenados en `PROGMEM`. La función `refrescarCara()` aplica la corrección comprobada para esta matriz:

```cpp
pantalla.drawPixel((x + 1) % 8, y, LED_ON);
```

Es un desplazamiento circular: conserva las ocho columnas. No añadir otro desplazamiento encima de este.

## Funcionamiento automático

```text
# → parar y centrar el cuello → comprobar el frente
                                  │
                    frente fiable a 35 cm o más
                                  ↓
                               avanzar
                                  │
                 obstáculo, lectura inválida o caducada
                                  ↓
                  parar → mirar izquierda y derecha
                                  ↓
                     elegir un lado con espacio
                                  ↓
               centrar cuello → girar un paso → parar
                                  └── volver a comprobar el frente
```

Antes de arrancar y al explorar cada lado, toma **tres medidas**, exige **al menos dos válidas** y utiliza la **menor distancia válida**. El código acepta lecturas entre **2 y 400 cm**; eso es un filtro del programa, no una garantía de precisión física en todo ese intervalo.

Para empezar o reanudar el avance exige **35 cm o más** delante. Una vez avanzando, se detiene y explora si detecta **25 cm o menos**, una lectura inválida o si pasan más de **300 ms** sin actualizar la lectura frontal válida.

Para escoger un giro exige **35 cm o más** en ese lado. Si ambos lados cumplen, elige el más despejado; cuando sus distancias difieren en **8 cm o menos**, alterna respecto al giro anterior. Centra el cuello antes de girar y vuelve a medir el frente al terminar.

Si no encuentra un paso fiable, permanece parado y reevalúa tras **1500 ms**. **Sin eco no significa camino libre.** No retrocede automáticamente.

Los giros duran **400 ms por paso**, no un ángulo calculado. Tras **seis pasos de giro sin avance suficiente**, deja de ordenar más giros y sigue comprobando el frente. El contador se reinicia al avanzar durante al menos un segundo o al salir y volver a entrar en automático.

## Ajustes del montaje

Modificar las constantes al principio del sketch y volver a subirlo. Al cambiar de versión, trasladar los ajustes que ya funcionen: no se guardan entre cargas ni en EEPROM.

### Sentido de ruedas y cuello

```cpp
const bool A_ES_IZQUIERDO = true;
const bool INVERTIR_A = false;
const bool INVERTIR_B = false;
const bool INVERTIR_CUELLO = false;
```

`A_ES_IZQUIERDO` indica qué rueda está conectada al canal A. Los indicadores `INVERTIR_A` e `INVERTIR_B` invierten el sentido de cada motor. `INVERTIR_CUELLO` intercambia la orientación izquierda/derecha del servo.

**Izquierda y derecha se entienden desde el robot mirando hacia delante.** Comprobar que ↑ mueve ambas ruedas hacia delante, que las flechas de giro actúan en el sentido esperado y que la primera mirada de exploración apunta a la izquierda del robot.

### Parámetros principales

| Parámetro | Valor inicial | Efecto |
|---|---:|---|
| `CUELLO_CENTRO` | 90° | Orientación frontal. |
| `CUELLO_IZQUIERDA` / `CUELLO_DERECHA` | 150° / 30° | Ángulos solicitados durante la exploración. |
| `ASENTAR_CUELLO_MS` | 650 ms | Espera antes de medir tras orientar el cuello. |
| `VELOCIDAD_AUTO` | 140 | PWM de avance automático. |
| `VELOCIDAD_GIRO` | 160 | PWM durante los giros automáticos. |
| `DISTANCIA_PARADA_CM` | 25 cm | Umbral de parada durante el avance. |
| `DISTANCIA_REANUDAR_CM` | 35 cm | Distancia mínima para arrancar o reanudar. |
| `DISTANCIA_LATERAL_CM` | 35 cm | Distancia mínima para considerar un lado libre. |
| `GIRO_PASO_MS` | 400 ms | Duración de cada paso de giro. |
| `ESPERA_BLOQUEADO_MS` | 1500 ms | Espera antes de reevaluar cuando no hay paso. |
| `MAX_GIROS_SIN_AVANZAR` | 6 | Límite de pasos de giro sin avance suficiente. |
| `PARADA_MANUAL_MS` | 300 ms | Tiempo sin órdenes antes de parar en manual. |
| `BRILLO` | 5 | Brillo de la matriz, de 0 a 15. |
| `DIAGNOSTICO_SONAR` | `true` | Registro de lecturas; **solo en V4.1**. |

La función del cuello limita los ángulos a **30–150°**. Reducir ese recorrido si el soporte roza alguna pieza; no forzar el servo. Calibrar los giros con espacio libre, empezando por pruebas cortas.

## Qué cambia exactamente en V4.1

V4.0 llama a `IrReceiver.begin(IR_PIN, DISABLE_LED_FEEDBACK)`. V4.1 conserva esa llamada y añade, **antes de incluir IRremote**:

```cpp
#define NO_LED_FEEDBACK_CODE
#include <IRremote.hpp>
```

Después de inicializar el receptor, reafirma el estado del pin de eco:

```cpp
pinMode(ECHO_PIN, INPUT);
digitalWrite(ECHO_PIN, LOW); // Sin pull-up interno.
```

La finalidad es reservar D13 para ECHO y evitar interferencias con la configuración del LED de IRremote. La biblioteca documenta `NO_LED_FEEDBACK_CODE` para [excluir ese código durante la compilación][irremote-led]. Es una corrección de compatibilidad; por sí sola no confirma que la causa de un fallo de lectura fuese ese conflicto.

V4.1 también añade `registrarSonar()` y `DIAGNOSTICO_SONAR`. Los mensajes de lectura se limitan a **uno cada 250 ms como máximo**, y se omiten si no caben en el búfer serie. No se muestra necesariamente cada medición.

### Diagnóstico por monitor serie — V4.1

Ejemplos de salida; las distancias son ilustrativas y no un registro de una prueba real:

```text
TURTLE V4.1 P2: #=AUTO/MANUAL; OK=parar
P2: TRIG=D12, ECHO=D13. IR sin LED D13.
AUTO: inicia con >=35 cm validos; sin eco no avanza.
AUTO: cara de malo; comprobando frente
SONAR FRENTE: 62 cm
AUTO: frente libre; avanzando
SONAR FRENTE: 22 cm
AUTO: parado; mirando ambos lados
SONAR IZQ: 80 cm
SONAR DER: 18 cm
Lados cm: I=78 D=17
AUTO: giro izquierda
```

| Mensaje | Significado y comprobación |
|---|---|
| `SIN_ECO` | No se completó un pulso de eco en 25 ms. Comprobar cableado, alimentación y una superficie frontal adecuada. No permite avanzar. |
| `ECO_ALTO` | ECHO ya estaba alto antes de disparar. Revisar P2 y que D13 no se utilice como salida, LED o SPI. |
| `ECO_FUERA_RANGO` | Se capturó un pulso que no produjo una distancia aceptada. Se trata como lectura inválida. |
| `Lados cm: I=-1 D=...` | `-1` indica que ese grupo no reunió suficientes medidas válidas; no significa espacio ilimitado. |
| `AUTO: sin paso fiable; esperando parado` | No hay un lado aceptable o se alcanzó el límite de giros. El robot permanece detenido y reevalúa. |
| `ERROR: matriz no detectada; motores bloqueados` | La inicialización de la matriz falló. Revisar alimentación, A4/A5 y dirección `0x70`; corregir y reiniciar. |
| `ERROR I2C: detenido. Revisar matriz y reiniciar.` | Se detectó un timeout de I²C. Esta comprobación solo se compila si el core ofrece `WIRE_HAS_TIMEOUT`. |

Para una primera prueba de lecturas, colocar una caja frente al sensor, aproximadamente a **50–100 cm**, con las ruedas levantadas, y pulsar `#`. Un frente a menos de 35 cm o sin eco válido no autoriza el arranque.

## Temporizadores e interrupciones

| Recurso | Uso en estos sketches |
|---|---|
| **Timer0** | Se mantiene su configuración para el reloj del programa y PWM del motor B en D5. |
| **Timer1** | Se conserva disponible para PWM del motor A en D9. |
| **Timer2** | Se selecciona para IRremote mediante `IR_USE_AVR_TIMER2`. |
| **Servo D10** | Pulsos por software, con periodo nominal de 20 ms; no utiliza `Servo.h`. |
| **PCINT0 / PB5 / PCINT5** | Captura de los flancos de ECHO en D13. |

La biblioteca estándar [Servo utiliza Timer1 en esta plataforma][servo-timers], y advierte que [el PWM asociado deja de estar disponible al adjuntar un servo][servo-pwm]. Por eso estos programas generan los pulsos del cuello por software. Cambiar únicamente el pin del servo no soluciona ese conflicto.

No añadir `tone()`, `ServoTimer2` o PWM en D3/D11 mientras Timer2 esté reservado para IRremote. La biblioteca explica los [conflictos por compartir temporizadores][irremote-timers]. Tampoco incorporar otra rutina `ISR(PCINT0_vect)` sin integrar ambos usos.

**No basta con cambiar `ECHO_PIN` para mover el ultrasónico:** la interrupción y sus máscaras están implementadas específicamente para D13. No usar ese pin para parpadear `LED_BUILTIN` ni para SPI en este montaje.

Evitar esperas largas y transferencias añadidas sin revisar el bucle: el servo depende de que `loop()` atienda sus pulsos regularmente. El ultrasónico no utiliza `pulseIn()` bloqueante; solo el disparo contiene una espera de 12 microsegundos.

## Problemas frecuentes

| Síntoma | Qué revisar primero |
|---|---|
| El canal A no funciona y B sí | `PWM_A = 9`, no 6; batería, contactos y ausencia de `Servo.h` en el programa. |
| Ambas ruedas giran igual y no se puede dirigir | No conectar ambas al grupo B/B1: se necesitan los dos canales independientes. |
| Manual no mueve el robot | Baterías, interruptor, inicialización de la matriz, receptor en P4/A1 y códigos del mando. Mantener pulsada una flecha. |
| Manual funciona, pero automático no arranca | Lecturas en V4.1, frente fiable a 35 cm o más y ultrasónico en P2. No eliminar la parada por falta de eco para ocultar el fallo. |
| ↑ produce un giro o una rueda gira al revés | Ajustar `INVERTIR_A` y `INVERTIR_B` con las ruedas levantadas. |
| Los giros o las miradas están invertidos | Revisar `A_ES_IZQUIERDO` e `INVERTIR_CUELLO`. |
| Los números no cambian la expresión | En automático es intencional. Pulsar `#` u `OK` para volver a manual. |
| El mando responde con otro programa, pero no aquí | Este código filtra NEC/NEC2, dirección `0x0000` y comandos concretos. Revisar la tabla siguiente. |

<details>
<summary>Códigos del mando definidos en los sketches</summary>

El programa compara `decodedIRData.command`, no el valor bruto completo de una trama. Solo acepta protocolos `NEC` / `NEC2` y dirección `0x0000`.

| Botón | Comando | Botón | Comando |
|---|---|---|---|
| ↑ | `0x46` | ↓ | `0x15` |
| ← | `0x44` | → | `0x43` |
| OK | `0x40` | # | `0x4A` |
| * | `0x42` | 0 | `0x52` |
| 1 | `0x16` | 2 | `0x19` |
| 3 | `0x0D` | 4 | `0x0C` |
| 5 | `0x18` | 6 | `0x5E` |
| 7 | `0x08` | 8 | `0x1C` |
| 9 | `0x5A` | | |

Para otro mando, obtener sus valores con un ejemplo de recepción de IRremote cargado **por separado** y adaptar `K_*`, `TECLAS_NUMERO` y, cuando corresponda, los filtros de `leerMando()`.

</details>

## Limitaciones y seguridad

Esta demo no es un sistema de navegación con garantías de evitar colisiones. **En manual no utiliza el ultrasónico para impedir movimientos.** En automático no detecta desniveles, no mide ángulos de giro y no comprueba obstáculos durante el propio giro. No utiliza los sensores de seguimiento de líneas.

La pérdida del mando detiene la conducción manual, **pero no cancela el automático**, que continúa según sus sensores. `OK` es una orden infrarroja, no un interruptor físico de emergencia: mantener accesible el interruptor de alimentación.

Usar bajo supervisión, en el suelo, con espacio despejado y lejos de escaleras y bordes. Primero verificar ruedas, cuello y medidas con las ruedas levantadas. Una ausencia de eco puede dejar el robot parado incluso si aparentemente hay espacio; es el comportamiento conservador previsto.

## Estado de validación

**Confirmado durante las pruebas del montaje:** ambos motores funcionan con A en D2/D9 y B en D4/D5; la matriz se visualiza correctamente con el desplazamiento circular de una columna.

**Pendiente de confirmar para estas versiones completas:** compatibilidad con las versiones exactas de las bibliotecas instaladas, funcionamiento conjunto de mando/servo/ultrasónico y comportamiento de V4.1 en automático. Este README se ha contrastado con los dos archivos fuente; al prepararlo no se ha realizado una compilación AVR ni una prueba física nueva.

## Referencias

La descripción del comportamiento y de los valores iniciales corresponde a los [sketches V4.0](Turtle_Mando_Caras_Auto/Turtle_Mando_Caras_Auto.ino) y [V4.1](Turtle_Mando_Caras_Auto_P2/Turtle_Mando_Caras_Auto_P2.ino) incluidos en el repositorio.

Documentación de apoyo: [shield KS0298][shield], [guía KS0464][kit], [Arduino IRremote][irremote], [Adafruit LED Backpack][backpack] y [formato de sketches de Arduino][sketch-spec].

[shield]: https://wiki.keyestudio.com/KS0298_Keyestudio_quick_connectors_Motor_Drive_Shield
[kit]: https://docs.keyestudio.com/projects/KS0464/en/latest/KS0464_arduino.html
[irremote]: https://github.com/Arduino-IRremote/Arduino-IRremote
[irremote-led]: https://github.com/Arduino-IRremote/Arduino-IRremote#feedback-led
[irremote-timers]: https://github.com/Arduino-IRremote/Arduino-IRremote#incompatibilities-to-other-libraries-and-arduino-commands-like-tone-and-analogwrite
[servo-timers]: https://github.com/arduino-libraries/Servo/blob/master/src/avr/ServoTimers.h
[servo-pwm]: https://github.com/arduino-libraries/Servo/blob/master/src/Servo.h
[backpack]: https://github.com/adafruit/Adafruit_LED_Backpack
[sketch-spec]: https://docs.arduino.cc/arduino-cli/sketch-specification/
