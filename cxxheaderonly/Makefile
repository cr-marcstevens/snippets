#ECFLAGS+=-g -ggdb -DDEBUG
ECFLAGS=-Ofast -march=native
all:
	$(CXX) test.cpp -std=c++11 -pthread -lpthread -o test -lomp -fopenmp ${ECFLAGS}
