/*
 * Simple Ticket Reservation Client with Visual Seat Map
 * 
 * Usage: ./client [server_ip] [port]
 *   server_ip - IP address of the server (default: 127.0.0.1)
 *   port      - Port number (default: 8080)
 * 
 * Examples:
 *   ./client                    - Connect to localhost:8080
 *   ./client 192.168.1.100      - Connect to 192.168.1.100:8080
 *   ./client 192.168.1.100 9000 - Connect to 192.168.1.100:9000
 * 
 * Commands (case-insensitive):
 *   available / avail / a  - Query available seats (shows visual map)
 *   book n s1 s2 ...       - Book n seats (s1, s2, ...)
 *   exit / quit / q        - Disconnect from server
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

/* Convert string to uppercase (in place) */
void to_upper(char* str) {
    for (int i = 0; str[i]; i++) {
        str[i] = toupper(str[i]);
    }
}

/* Parse available seats from server response and display visual map */
void display_seat_map(char* response) {
    int seat_count = 0;
    int seats_status[MAX_SEATS + 1] = {0}; /* 1-indexed: seats_status[1] to seats_status[20] */
    
    /* Parse the response: "AVAILABLE 1 2 3 ..." or "AVAILABLE NONE" */
    char* token = strtok(response, " \t\n");
    token = strtok(NULL, " \t\n"); /* Skip "AVAILABLE" */
    
    if (token == NULL || strcmp(token, "NONE") == 0) {
        printf("\n\t\t* * * * * * * * * * * *   S\tC\tR\tE\tE\tN   * * * * * * * * * * * * *\n");
        printf("\nAll seats are booked!\n\n");
        return;
    }
    
    /* Parse all available seat numbers */
    while (token != NULL && seat_count < MAX_SEATS) {
        int seat_num = atoi(token);
        if (seat_num >= 1 && seat_num <= MAX_SEATS) {
            seat_count++;
            seats_status[seat_num] = 1; /* Mark as available */
        }
        token = strtok(NULL, " \t\n");
    }
    
    /* Display visual seat map (4 rows x 5 columns) */
    printf("\n\t\t* * * * * * * * * * * *   S\tC\tR\tE\tE\tN   * * * * * * * * * * * * *\n");
    printf("\nSeat Map (Available seats are shown with numbers, Booked seats are marked as [X]):\n\n");
    printf("        ");
    for (int col = 1; col <= 5; col++) {
        printf("Col %d\t", col);
    }
    printf("\n");
    
    for (int row = 0; row < 4; row++) {
        printf("Row %d:  ", row + 1);
        for (int col = 0; col < 5; col++) {
            int seat_num = row * 5 + col + 1;
            if (seats_status[seat_num]) {
                printf("[%2d]\t", seat_num); /* Available seat */
            } else {
                printf("[ X]\t"); /* Booked seat */
            }
        }
        printf("\n");
    }
    
    printf("\nLegend: [XX] = Available, [ X] = Booked\n");
    printf("Total available seats: %d\n\n", seat_count);
}

/* Normalize command to uppercase and handle aliases */
void normalize_command(char* command, char* normalized) {
    char cmd_copy[BUFFER_SIZE];
    strncpy(cmd_copy, command, BUFFER_SIZE - 1);
    cmd_copy[BUFFER_SIZE - 1] = '\0';
    
    /* Remove leading/trailing whitespace */
    int start = 0;
    while (cmd_copy[start] == ' ' || cmd_copy[start] == '\t') start++;
    int end = strlen(cmd_copy) - 1;
    while (end >= 0 && (cmd_copy[end] == ' ' || cmd_copy[end] == '\t' || cmd_copy[end] == '\n' || cmd_copy[end] == '\r')) {
        cmd_copy[end] = '\0';
        end--;
    }
    
    if (start > end) {
        normalized[0] = '\0';
        return;
    }
    
    /* Convert to uppercase for comparison */
    to_upper(cmd_copy + start);
    
    /* Handle command aliases */
    if (strncmp(cmd_copy + start, "AVAILABLE", 9) == 0 || 
        strncmp(cmd_copy + start, "AVAIL", 5) == 0 ||
        strcmp(cmd_copy + start, "A") == 0) {
        strcpy(normalized, "AVAILABLE");
    }
    else if (strncmp(cmd_copy + start, "BOOK", 4) == 0 || 
             strncmp(cmd_copy + start, "B", 1) == 0) {
        /* Keep the full BOOK command with arguments */
        strcpy(normalized, cmd_copy + start);
    }
    else if (strcmp(cmd_copy + start, "EXIT") == 0 || 
             strcmp(cmd_copy + start, "QUIT") == 0 ||
             strcmp(cmd_copy + start, "Q") == 0) {
        strcpy(normalized, "EXIT");
    }
    else {
        /* Unknown command, pass as-is */
        strcpy(normalized, cmd_copy + start);
    }
}

int main(int argc, char* argv[]) {
    int sock_fd;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];
    char command[BUFFER_SIZE];
    const char* server_ip = "127.0.0.1";  /* Default to localhost */
    int port = DEFAULT_PORT;
    
    /* Parse command-line arguments */
    if (argc >= 2) {
        server_ip = argv[1];
    }
    if (argc >= 3) {
        port = atoi(argv[2]);
        if (port <= 0 || port > 65535) {
            fprintf(stderr, "Error: Invalid port number. Must be between 1 and 65535.\n");
            exit(EXIT_FAILURE);
        }
    }
    
    if (argc > 3) {
        fprintf(stderr, "Usage: %s [server_ip] [port]\n", argv[0]);
        fprintf(stderr, "  server_ip - IP address of server (default: 127.0.0.1)\n");
        fprintf(stderr, "  port      - Port number (default: %d)\n", DEFAULT_PORT);
        exit(EXIT_FAILURE);
    }
    
    /* Create socket */
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    
    /* Configure server address */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    
    /* Convert IP address from string to binary */
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "Error: Invalid IP address: %s\n", server_ip);
        exit(EXIT_FAILURE);
    }
    
    /* Connect to server */
    printf("Connecting to server at %s:%d...\n", server_ip, port);
    if (connect(sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        printf("Make sure the server is running on %s:%d\n", server_ip, port);
        exit(EXIT_FAILURE);
    }
    
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║     Welcome to Ticket Reservation System!                  ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");
    printf("\nConnected to server at %s:%d\n\n", server_ip, port);
    printf("Commands (case-insensitive):\n");
    printf("  available / avail / a  - Show available seats (visual map)\n");
    printf("  book n s1 s2 ...       - Book n seats (e.g., book 2 5 10)\n");
    printf("  exit / quit / q        - Disconnect\n\n");
    
    /* Main loop: read commands from stdin and send to server */
    while (1) {
        printf("> ");
        fflush(stdout);
        
        /* Read command from stdin */
        if (fgets(command, BUFFER_SIZE, stdin) == NULL) {
            break; /* EOF or error */
        }
        
        /* Normalize command (handle case and aliases) */
        char normalized[BUFFER_SIZE];
        normalize_command(command, normalized);
        
        if (strlen(normalized) == 0) {
            continue; /* Empty command, ignore */
        }
        
        /* Check for EXIT command */
        if (strcmp(normalized, "EXIT") == 0) {
            send(sock_fd, "EXIT\n", 5, 0);
            break;
        }
        
        /* Send normalized command to server (add newline for line-oriented protocol) */
        char command_to_send[BUFFER_SIZE + 1];
        int len = snprintf(command_to_send, BUFFER_SIZE, "%s\n", normalized);
        if (len >= BUFFER_SIZE) {
            command_to_send[BUFFER_SIZE - 1] = '\n';
            command_to_send[BUFFER_SIZE] = '\0';
        }
        if (send(sock_fd, command_to_send, strlen(command_to_send), 0) < 0) {
            perror("Send failed");
            break;
        }
        
        /* Receive response from server */
        ssize_t bytes_received = recv(sock_fd, buffer, BUFFER_SIZE - 1, 0);
        if (bytes_received <= 0) {
            if (bytes_received == 0) {
                printf("Server closed connection\n");
            } else {
                perror("Receive failed");
            }
            break;
        }
        
        buffer[bytes_received] = '\0';
        
        /* Check if response is AVAILABLE and display visual map */
        if (strncmp(buffer, "AVAILABLE", 9) == 0) {
            char response_copy[BUFFER_SIZE];
            strncpy(response_copy, buffer, BUFFER_SIZE - 1);
            response_copy[BUFFER_SIZE - 1] = '\0';
            display_seat_map(response_copy);
        } else {
            /* Print other responses normally */
            printf("Server: %s", buffer);
        }
    }
    
    close(sock_fd);
    printf("Disconnected from server\n");
    return 0;
}

