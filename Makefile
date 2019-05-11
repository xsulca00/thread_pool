all:
	g++ -std=c++17 -O3 -march=native -Wall -Wextra -pedantic -o run scoped_thread.cpp -pthread
