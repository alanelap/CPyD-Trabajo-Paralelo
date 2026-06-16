# Procesamiento Paralelo de Transacciones SFTP

Este proyecto es una solución de alto rendimiento en C++ que utiliza **OpenMP** y **libcurl** para procesar archivos CSV descargados desde un servidor SFTP y consultar masivamente una API REST. Su objetivo es calcular el promedio de compras realizadas por clientes segmentados por género (Femenino y Masculino) en un entorno de ejecución altamente concurrente.

## Requisitos del Sistema
* **Entorno de Evaluación:** Ubuntu 24.04 LTS.
* **Compilador:** GCC (Gnu C Compiler suit) compatible con C++17.
* **Librerías:** `libcurl4-openssl-dev`, `nlohmann-json3-dev` y soporte nativo para `OpenMP`.

## Compilación (Ubuntu 24.04 LTS)

Para compilar el proyecto utilizando el archivo `Makefile` provisto, abre una terminal en la raíz del proyecto y ejecuta:

```bash
make clean
make

Ejecución Nativa
Por motivos de seguridad arquitectónica, el programa lee las credenciales del servidor SFTP y de la API directamente desde las variables de entorno.  
CPP
Para ejecutar el programa nativamente en Ubuntu, utiliza el siguiente comando en la terminal. Asegúrate de reemplazar TU_CORREO y TU_RUT con tus credenciales de acceso a la API:

API_EMAIL="TU_CORREO" API_RUT="TU_RUT" SFTP_USER="utem" SFTP_PASS="CPyD.2026" SFTP_HOST="137.184.45.251" ./tarea_paralela

Ejecución alternativa (Docker)
Si se desea ejecutar el programa en un entorno aislado, se incluye un Dockerfile configurado con Ubuntu 24.04. Asegúrate de tener un archivo .env con las credenciales en la raíz del proyecto:

docker build -t trabajo-cpyd .
docker run -it --name ejecucion_prueba --env-file .env -v "$(pwd)/data:/app/data" trabajo-cpyd

Resultados Obtenidos

Tras procesar la totalidad de los datos (aproximadamente 3 millones de transacciones/clientes únicos), los resultados arrojados por el sistema fueron:

FEMENINO = 12463.81  

MASCULINO = 13000.75  

TIEMPO = 25610.27 segundos  

Análisis y Explicación de los Resultados
Promedios de Compra: Los valores obtenidos representan el ticket promedio de compra (en pesos chilenos) para cada género a lo largo de todas las sucursales
y fechas procesadas. Se descartaron del cálculo aquellas transacciones cuyos UUIDs no existían en la API o no tenían un género definido.  
PDF

Tiempo de Ejecución: El tiempo total de 25610.27 segundos (aproximadamente 7.11 horas) refleja el procesamiento completo del flujo de trabajo (sincronización y
validación SFTP, parseo local de CSVs en memoria y consulta masiva asíncrona a la API REST).

Optimización de Red (I/O Bound): Para lograr este tiempo y evitar una ejecución secuencial proyectada en más de 110 horas,
se implementó una arquitectura de Over-subscription utilizando 600 hilos concurrentes de OpenMP para saturar el servidor asíncronamente.
Además, se configuró libcurl con CURLOPT_TCP_KEEPALIVE para mantener túneles TCP persistentes,
eliminando el severo cuello de botella que producía el SSL Handshake repetitivo contra la API de la universidad.  

Estabilidad: Mediante la reducción de secciones críticas y el control de Lock Contention en la salida estándar (terminal), el sistema mantuvo un consumo de memoria estable (1.62 GB) sin presentar fugas de memoria (memory leaks) a lo largo de las 7 horas de estrés continuo.
