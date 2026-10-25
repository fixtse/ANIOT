# Ejercicio 2: comunicación mediante colas

Este proyecto parte del ejercicio 1 (lectura periódica del sensor SHTC3 por I2C) y cambia la comunicación entre `app_main` y la tarea muestreadora para usar una cola de FreeRTOS.

## Funcionamiento

- `app_main` inicializa I2C y el sensor, crea una cola de cinco muestras y arranca `sampling_task`.
- La tarea recibe mediante su argumento una estructura con el período de muestreo (`SAMPLE_PERIOD_MS`) y el identificador de la cola.
- Cada lectura correcta se copia en la cola con `xQueueSend`.
- `app_main` espera muestras con `xQueueReceive` y muestra temperatura y humedad. La recepción bloqueante evita consultar la cola repetidamente cuando no hay datos.

El envío usa `portMAX_DELAY`: si la cola se llena, la tarea espera a que `app_main` libere espacio. Los errores de lectura del sensor se registran y no se envía una muestra inválida.

## Compilación

Desde este directorio, configura el objetivo de tu placa y compila con ESP-IDF:

```sh
idf.py set-target esp32
idf.py build
```

Ajusta el objetivo si utilizas otra placa compatible. Las conexiones I2C están configuradas en `main/hello_world_main.c` (SCL GPIO 8, SDA GPIO 10).
