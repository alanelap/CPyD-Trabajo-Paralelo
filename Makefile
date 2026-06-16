CXX = g++
CXXFLAGS = -Wall -O3 -std=c++17 -pthread -fopenmp
LDFLAGS = -lcurl -fopenmp

all: tarea_paralela tester

tarea_paralela: tarea_paralela.cpp
	$(CXX) $(CXXFLAGS) tarea_paralela.cpp -o tarea_paralela $(LDFLAGS)

tester: tarea_tester_conexion.cpp
	$(CXX) $(CXXFLAGS) tarea_tester_conexion.cpp -o tester $(LDFLAGS)

clean:
	rm -f tarea_paralela tester log.txt resultados*.txt
