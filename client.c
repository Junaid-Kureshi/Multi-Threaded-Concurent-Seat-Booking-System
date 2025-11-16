/*
 * Ticket Reservation Client with Visual Seat Map
 * Usage: ./client [server_ip] [port]
 * Commands (case-insensitive): available/a, book n s1 s2..., cancel n s1 s2..., exit/q
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h>

#define DEFAULT_PORT 8080
#define BUFFER_SIZE 1024
#define MAX_SEATS 20

void to_upper(char* str) {
    for (int i = 0; str[i]; i++) str[i] = toupper(str[i]);
}

void display_seat_map(char* response) {
    int seats_status[MAX_SEATS + 1] = {0};
    int count = 0;
    
    char* token = strtok(response, " \t\n");
    token = strtok(NULL, " \t\n");
    
    if (!token || strcmp(token, "NONE") == 0) {
        printf("\n\t\t* * * * * * * * * * * *   S\tC\tR\tE\tE\tN   * * * * * * * * * * * * *\n");
        printf("\nAll seats booked!\n\n");
        return;
    }
    
    while (token && count < MAX_SEATS) {
        int seat = atoi(token);
        if (seat >= 1 && seat <= MAX_SEATS) {
            seats_status[seat] = 1;
            count++;
        }
        token = strtok(NULL, " \t\n");
    }
    
    printf("\n\t\t* * * * * * * * * * * *   S\tC\tR\tE\tE\tN   * * * * * * * * * * * * *\n");
    printf("\nSeat Map ([XX]=Available, [ X]=Booked):\n\n        ");
    for (int col = 1; col <= 5; col++) printf("Col %d\t", col);
    printf("\n");
    
    for (int row = 0; row < 4; row++) {
        printf("Row %d:  ", row + 1);
        for (int col = 0; col < 5; col++) {
            int seat = row * 5 + col + 1;
            printf(seats_status[seat] ? "[%2d]\t" : "[ X]\t", seat);
        }
        printf("\n");
    }
    printf("\nAvailable: %d seats\n\n", count);
}

void normalize_command(char* command, char* normalized) {
    char cmd_copy[BUFFER_SIZE];
    strncpy(cmd_copy, command, BUFFER_SIZE - 1);
    cmd_copy[BUFFER_SIZE - 1] = '\0';
    
    int start = 0;
    while (cmd_copy[start] == ' ' || cmd_copy[start] == '\t') start++;
    int end = strlen(cmd_copy) - 1;
    while (end >= 0 && (cmd_copy[end] == ' ' || cmd_copy[end] == '\t' || cmd_copy[end] == '\n' || cmd_copy[end] == '\r')) {
        cmd_copy[end--] = '\0';
    }
    
    if (start > end) {
        normalized[0] = '\0';
        return;
    }
    
    to_upper(cmd_copy + start);
    
    if (strncmp(cmd_copy + start, "AVAILABLE", 9) == 0 || strncmp(cmd_copy + start, "AVAIL", 5) == 0 || strcmp(cmd_copy + start, "A") == 0) {
        strcpy(normalized, "AVAILABLE");
    } else if (strncmp(cmd_copy + start, "BOOK", 4) == 0 || strncmp(cmd_copy + start, "B", 1) == 0) {
        strcpy(normalized, cmd_copy + start);
    } else if (strncmp(cmd_copy + start, "CANCEL", 6) == 0 || strncmp(cmd_copy + start, "C", 1) == 0) {
        strcpy(normalized, cmd_copy + start);
    } else if (strcmp(cmd_copy + start, "EXIT") == 0 || strcmp(cmd_copy + start, "QUIT") == 0 || strcmp(cmd_copy + start, "Q") == 0) {
        strcpy(normalized, "EXIT");
    } else {
        strcpy(normalized, cmd_copy + start);
    }
}

int main(int argc, char* argv[]) {
    const char* server_ip = (argc >= 2) ? argv[1] : "127.0.0.1";
    int port = (argc >= 3) ? atoi(argv[2]) : DEFAULT_PORT;
    
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Error: Invalid port\n");
        exit(EXIT_FAILURE);
    }
    
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }
    
    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "Error: Invalid IP\n");
        exit(EXIT_FAILURE);
    }
    
    printf("Connecting to %s:%d...\n", server_ip, port);
    if (connect(sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        exit(EXIT_FAILURE);
    }
    
    printf("\n╔════════════════════════════════════════════════════════════╗\n");
    printf("║     Welcome to Ticket Reservation System!                  ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");
    printf("\nConnected! Commands: available/a, book n s1 s2..., cancel n s1 s2..., exit/q\n\n");
    
    char buffer[BUFFER_SIZE], command[BUFFER_SIZE];
    
    while (1) {
        printf("> ");
        fflush(stdout);
        
        if (!fgets(command, BUFFER_SIZE, stdin)) break;
        
        char normalized[BUFFER_SIZE];
        normalize_command(command, normalized);
        
        if (strlen(normalized) == 0) continue;
        if (strcmp(normalized, "EXIT") == 0) {
            send(sock_fd, "EXIT\n", 5, 0);
            break;
        }
        
        char cmd_send[BUFFER_SIZE + 2];
        int len = snprintf(cmd_send, BUFFER_SIZE + 1, "%s\n", normalized);
        if (len >= BUFFER_SIZE + 1) cmd_send[BUFFER_SIZE] = '\n';
        if (send(sock_fd, cmd_send, strlen(cmd_send), 0) < 0) break;
        
        ssize_t bytes = recv(sock_fd, buffer, BUFFER_SIZE - 1, 0);
        if (bytes <= 0) {
            printf(bytes == 0 ? "Server closed connection\n" : "Receive failed\n");
            break;
        }
        
        buffer[bytes] = '\0';
        if (strncmp(buffer, "AVAILABLE", 9) == 0) {
            char response_copy[BUFFER_SIZE];
            strncpy(response_copy, buffer, BUFFER_SIZE - 1);
            response_copy[BUFFER_SIZE - 1] = '\0';
            display_seat_map(response_copy);
        } else {
            printf("Server: %s", buffer);
        }
    }
    
    close(sock_fd);
    printf("Disconnected\n");
    return 0;
}
