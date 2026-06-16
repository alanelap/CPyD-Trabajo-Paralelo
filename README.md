# Procesamiento Paralelo de Transacciones SFTP

## Requisitos
- Compilador GCC compatible con C++17.
- Librerías: `libcurl` y soporte para `OpenMP`.
- Entorno: Ubuntu 24.04 LTS.

## Compilación
Para compilar ambos programas (secuencial y paralelo), ejecuta en la terminal:
make

## Ejecución
Es obligatorio pasar las credenciales de la API como variables de entorno al ejecutar.

**Versión Paralela:**
OMP_NUM_THREADS=$(nproc) API_EMAIL="tu_correo@utem.cl" API_RUT="tu_rut" ./tarea_paralela

**Versión Secuencial:**
API_EMAIL="tu_correo@utem.cl" API_RUT="tu_rut" ./tarea_secuencial

## Limpieza
Para borrar los ejecutables y archivos de log generados:
make clean