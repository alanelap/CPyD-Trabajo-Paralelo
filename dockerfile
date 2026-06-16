# Usamos exactamente la versión que pide la rúbrica
FROM ubuntu:24.04

# Evitar que la instalación de paquetes nos pida confirmaciones interactivas
ENV DEBIAN_FRONTEND=noninteractive

# Instalamos el compilador de C++ y las librerías del profesor
RUN apt-get update && apt-get install -y \
    build-essential \
    libcurl4-openssl-dev \
    nlohmann-json3-dev \
    && rm -rf /var/lib/apt/lists/*

# Creamos la carpeta de trabajo dentro del contenedor
WORKDIR /app

# Copiamos nuestro código fuente y el Makefile
COPY tarea_paralela.cpp tarea_tester_conexion.cpp Makefile ./

# Compilamos el código al momento de construir la imagen
RUN make clean && make

# Comando por defecto al iniciar el contenedor
CMD ["./tarea_paralela"]
