ECFLAGS=-Ofast -march=native
#ECFLAGS+=-g -ggdb -DDEBUG
all:
	$(CXX) test.cpp -std=c++11 -pthread -lpthread -o test -lomp -fopenmp ${ECFLAGS}
