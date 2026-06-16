# Procesamiento Paralelo de Transacciones SFTP

Este proyecto es una solución de alto rendimiento en C++ que utiliza **OpenMP** y **libcurl** para procesar archivos CSV descargados desde un servidor SFTP y consultar masivamente una API REST[cite: 2]. Su objetivo es calcular el promedio de compras realizadas por clientes segmentados por género (Femenino y Masculino) en un entorno de ejecución altamente concurrente[cite: 2].

## Requisitos del Sistema
* **Entorno de Evaluación:** Ubuntu 24.04 LTS[cite: 2].
* **Compilador:** GCC (Gnu C Compiler suit) compatible con C++17[cite: 2, 5].
* **Librerías:** `libcurl4-openssl-dev`, `nlohmann-json3-dev` y soporte nativo para `OpenMP`[cite: 5, 6].

## Compilación (Ubuntu 24.04 LTS)

Para compilar el proyecto utilizando el archivo `Makefile` provisto, abre una terminal en la raíz del proyecto y ejecuta[cite: 5]:

```bash
make clean
make
