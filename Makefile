CC = g++
CXXFLAGS = -std=c++23 -O2 -Wall -Werror -Wpedantic -Wextra -Wconversion -Wshadow -Wunused -Wold-style-cast

test_circuit.o: circuits/pres.hpp circuits/pres.cpp
	$(CC) $(CXXFLAGS) -c $@

test_circuit: test_circuit.o pres.o
	$(CC) $(CXXFLAGS) -o $@ $^

.PHONY: test_smtlib
test_smtlib:
	$(CC) $(CXXFLAGS) tests/test_smtlib.cpp circuits/smtlib.cpp -o $@
