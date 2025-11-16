# Makefile for Ticket Reservation System
# Compiles server and client programs with pthread support

CC = gcc
CFLAGS = -Wall -Wextra -pthread
SERVER_TARGET = server
CLIENT_TARGET = client
SERVER_SRC = server.c
CLIENT_SRC = client.c

.PHONY: all clean server client

all: server client

server: $(SERVER_SRC)
	$(CC) $(CFLAGS) -o $(SERVER_TARGET) $(SERVER_SRC)
	@echo "Server compiled successfully"

client: $(CLIENT_SRC)
	$(CC) $(CFLAGS) -o $(CLIENT_TARGET) $(CLIENT_SRC)
	@echo "Client compiled successfully"

clean:
	rm -f $(SERVER_TARGET) $(CLIENT_TARGET)
	@echo "Cleaned build artifacts"

# Quick test: compile and show usage
help:
	@echo "Usage:"
	@echo "  make          - Build both server and client"
	@echo "  make server   - Build only server"
	@echo "  make client   - Build only client"
	@echo "  make clean    - Remove compiled binaries"
	@echo ""
	@echo "To run:"
	@echo "  Terminal 1: ./server"
	@echo "  Terminal 2: ./client"

