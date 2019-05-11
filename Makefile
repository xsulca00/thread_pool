all:
	g++ -std=c++17 -O2 -Wall -Wextra -pedantic -o run scoped_thread.cpp -pthread
