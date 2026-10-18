CC = g++
CXXFLAGS = -std=c++23 -Wall -Werror -Wpedantic -Wextra -Wconversion -Wshadow -Wunused -Wold-style-cast

pres.o: circuits/pres.cpp circuits/pres.hpp
	$(CC) $(CXXFLAGS) -c $@ $<

test_circuit.o: circuits/pres.hpp circuits/pres.cpp
	$(CC) $(CXXFLAGS) -c $@

test_circuit: test_circuit.o pres.o
	$(CC) $(CXXFLAGS) -o $@ $^