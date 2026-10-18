# Volantes y FFB nativo experimental

Esta integración adapta el modelo de efectos de `fm4-recomp/src/wheel_ffb.cpp`
y `wheel_effects.h` a SDL2 y a la ejecución PowerPC de este fork. No incluye
código generado, ejecutables ni datos del juego. FM4 conserva sus cálculos de
física y fuerza; se traducen sus efectos nativos, sin convertir rumble en par.

## Alcance

- Entrada por joystick SDL sin lista de marcas: volante, acelerador, freno,
  botones, levas y primer POV, en el jugador 1.
- FFB mediante las funciones comunes `XInputdFF` de Xbox 360. No depende de
  direcciones, firmas ni identificadores de FM4: cualquier título puede utilizar
  esta vía. La compatibilidad física de cada juego debe comprobarse.
- Efectos constante, muelle, amortiguación, inercia y fricción. Los efectos
  condicionales que el controlador no soporte se omiten. Fuerza constante y
  control de ganancia son necesarios.
- FFB apagado inicialmente. Ganancia de hardware inicial: 10%, ajustable
  entre 0 y 100%. La inversión afecta a la fuerza constante.
- La implementación requiere validación física; las pruebas automatizadas
  utilizan un volante virtual y no envían fuerza a ningún dispositivo.

No se incluye todavía embrague proporcional, combinación de pedales USB separados ni efectos periódicos/rampas. La entrada del volante sí puede usarse en otros
títulos compatibles con la distribución de ejes de XInput.

## Configuración

### Desde el emulador

1. Abre **HID → Wheel setup...**, sin un juego en ejecución.
2. Selecciona el MOZA R3 en la lista. Si no aparece, conéctalo en modo PC y
   pulsa **Refresh**.
3. En **Calibration**, pulsa **Start guided calibration**. Sigue los ocho
   pasos: centro y pedales sueltos, acelerador pisado/suelto, freno pisado/suelto,
   extremo izquierdo, extremo derecho y centro. Mantén cada posición y pulsa
   **Capture and continue**. Los ejes y el sentido de los pedales se detectan
   automáticamente; las capturas ambiguas se rechazan para volver a intentarlo.
4. Comprueba las barras de dirección, acelerador y freno. La pestaña
   **Live axes** muestra además todos los ejes físicos.
5. En **Buttons and paddles**, pulsa **Assign** junto a una acción y después el
   botón físico. Usa LB/RB para las levas si esa es la distribución del juego.
   La primera cruceta de tipo POV se reconoce automáticamente.
6. Opcionalmente ajusta la pestaña **FFB**. El soporte nativo usa la interfaz común de Xbox 360 y permanece apagado por defecto. La ventana no envía fuerzas
   de prueba.
7. Pulsa **Save and enable wheel**, cierra y vuelve a abrir el emulador.
   Se guardan el método `wheel`, el dispositivo, la calibración y los botones.

Cancelar una calibración conserva la anterior. Cerrar la ventana sin guardar
descarta sus cambios. Puedes inspeccionar entradas durante una partida, pero
debes cerrar el juego para guardar la configuración.

### Edición manual (opcional)

Cierra el emulador antes de editar `xenia-canary.config.toml`. Selecciona el
backend `wheel` en la opción `hid` existente. Este método selecciona por
defecto el MOZA R3 y presenta el jugador 1 como volante Xbox 360 en cualquier
juego. También puedes usar `sdl` con `wheel_name` explícito. En `[HID]` configura:

```toml
hid = "wheel"
wheel_name = "Gudsen R3 Racing Wheel and Pedals"
wheel_calibrated = false
wheel_log_axes = true
wheel_steering_axis = 0
wheel_throttle_axis = 2
wheel_brake_axis = 5
wheel_left = -32768
wheel_center = 0
wheel_right = 32767
wheel_throttle_released = 32767
wheel_throttle_pressed = -32768
wheel_brake_released = 32767
wheel_brake_pressed = -32768
wheel_buttons = "0:4096,1:8192,2:16384,3:32768,4:256,5:512,6:32,7:16"
wheel_ffb = false
wheel_ffb_gain = 10
wheel_ffb_invert = false
```

Estos valores son un ejemplo, no una calibración verificada para tu unidad.
Los ejes 2/5 proceden del perfil R3 del proyecto de referencia. El nombre debe
coincidir exactamente con SDL. Con un nombre provisional no vacío, el registro
muestra `Wheel candidate` para cada joystick conectado. La selección por
nombre no distingue dos unidades idénticas: conecta una sola.

1. Conecta la base en modo PC con su controlador habitual. Configura su
   rotación antes de calibrar.
2. Consulta `Wheel raw axes` en el registro mientras mueves cada control.
   Anota los ejes, centro y extremos de dirección y valores de cada pedal
   liberado y totalmente pisado. Se registran como máximo una vez por segundo.
3. Guarda esos valores en las opciones anteriores y cambia
   `wheel_calibrated = true`. Se aceptan ejes/pedales invertidos; los tres ejes
   deben ser distintos. Una calibración inválida deja los ejes neutros y FFB
   inactivo, manteniendo botones y POV.
4. Ajusta `wheel_buttons`: cada par es `botón físico:máscara Xbox decimal`.
   A=4096, B=8192, X=16384, Y=32768, LB=256, RB=512, Back=32, Start=16;
   D-pad arriba=1, abajo=2, izquierda=4, derecha=8. Usa LB/RB para las levas
   según la distribución elegida dentro del juego. Botones desde índice 0.
5. Comprueba dirección y pedales en el juego. Después activa `wheel_ffb = true`
   y conserva inicialmente la ganancia del 10%. Busca en el registro
   `Wheel native FFB: available`. Cuando el juego consulte el dispositivo,
   aparecerá `Wheel native FFB: device information requested`. Los efectos
   no admitidos se indican en el registro.
6. Cuando termines la calibración, desactiva `wheel_log_axes`.

Con el modo de volante seleccionado, el jugador 1 del backend SDL usa el
dispositivo elegido. Los motores de vibración de gamepad no se envían a él.

## Ciclo de salida

Los accesos a joystick y haptics se realizan en el hilo de interfaz de SDL.
Los hilos invitados intercambian comandos bajo mutex. La salida exige foco,
calibración válida, efectos iniciados por el juego, con un
heartbeat de menos de 200 ms. Los efectos duran 100 ms y se renuevan cada
32 ms mientras el juego sigue enviando actividad. La pérdida de foco, cierre,
desconexión o actividad caducada detiene la salida en la siguiente actualización;
sin nuevas actualizaciones, los efectos expiran por hardware a los 100 ms.

Las exportaciones XInputdFF verifican los buffers invitados y completan las
peticiones de forma síncrona con estados NTSTATUS. Soportan consulta, creación,
actualización, inicio/parada, reset y ganancia global. La ganancia del juego
multiplica el límite del usuario, nunca lo aumenta. La actividad se renueva
solo con llamadas del juego, incluyendo su lectura de controles. Sin un volante
FFB disponible devuelven dispositivo desconectado. Solo se usa el jugador 1.
No se instalan hooks ni se modifica código de ningún juego. Los tipos de efecto
y comandos todavía no implementados devuelven no soportado. No se garantiza
compatibilidad con todos los juegos sin probar sus llamadas y efectos.

## Compilación y pruebas

Configura el proyecto con `-DXENIA_BUILD_WHEEL_TESTS=ON`, después de generar
`version.h` con el flujo habitual de `xb`. Compila `xenia-wheel-test` y
`xenia-wheel-hook-test` y `xenia-wheel-calibration-test`, y ejecuta
`ctest --test-dir build -R wheel --output-on-failure`.
`xenia-wheel-probe` muestra dispositivos y ejes en modo de solo lectura para
ayudar a identificar la base y calibrarla; nunca inicializa haptics.

Las pruebas comprueban tipos y escalado de efectos, ganancia cero, saturación,
inversión, rechazo de slots/tipos inválidos, entrada sin calibración, botones
de índices altos, POV, dirección, pedales independientes/invertidos, ejes
duplicados y desconexión. La prueba del JIT ejecuta un programa PowerPC
sintético, verifica el hook, la vuelta al cuerpo original, la pila y dirección
de retorno, y una función ordinaria sin hook. No sustituyen una prueba de
conducción con la base. La prueba de calibración comprueba el recorrido guiado,
pedales y dirección invertidos, movimientos ambiguos, posiciones repetidas,
cancelación y cambio de dispositivo durante una captura.

## FM2: effect activation

FM2 loads five effect slots, submits operation 2 to activate them, and then
updates their parameters while driving. Interpreting this operation as stop
leaves every slot inactive, even when all subsequent updates succeed. The
regression test checks activation of all five loaded slots. Operation 1 stops
an effect; operation 0 is accepted as stop as well.

The log records the first accepted/rejected native requests, with NTSTATUS on
failure, and the first SDL effect submissions or errors. Device-info queries
are logged once to avoid flooding the log.
