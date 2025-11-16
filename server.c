/*
 * Multi-threaded Ticket Reservation Server
 * 
 * Protocol:
 *   AVAILABLE          - Returns list of available seats
 *   BOOK n s1 s2 ...   - Attempts to book n seats (s1, s2, ...). Atomic: all or nothing.
 *   EXIT               - Client disconnects gracefully
 * 
 * Server responses:
 *   AVAILABLE <seat_list>     - List of available seat numbers
 *   OK BOOKED <seat_list>     - Successfully booked seats
 *   FAIL <reason>             - Booking failed with reason
 * 
 * Concurrency Control:
 *   Uses a single pthread_mutex_t (seats_mutex) to protect the entire seat array.
 *   All seat operations (check availability, book) happen within the critical section
 *   to prevent race conditions and double-booking.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>
#include <ctype.h>

#define PORT 8080
#define MAX_SEATS 20
#define BUFFER_SIZE 1024
#define MAX_CLIENTS 100

/* Seat structure */
struct seat {
    int id;           /* Seat number (1-20) */
    int booked;       /* 0 = available, 1 = booked */
    int booked_by;    /* Client socket descriptor (for logging) */
};

/* Global seat array - protected by seats_mutex */
struct seat seats[MAX_SEATS];
pthread_mutex_t seats_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Function prototypes */
void init_seats(void);
void* handle_client(void* arg);
int process_command(int client_fd, char* command, struct sockaddr_in* client_addr);
int handle_available(int client_fd);
int handle_book(int client_fd, char* args, struct sockaddr_in* client_addr);
void log_request(const char* action, struct sockaddr_in* client_addr, const char* result);

/* Initialize all seats as available */
void init_seats(void) {
    for (int i = 0; i < MAX_SEATS; i++) {
        seats[i].id = i + 1;
        seats[i].booked = 0;
        seats[i].booked_by = -1;
    }
}

/* Log request with timestamp and client info */
void log_request(const char* action, struct sockaddr_in* client_addr, const char* result) {
    time_t now = time(NULL);
    char* time_str = ctime(&now);
    time_str[strlen(time_str) - 1] = '\0'; /* Remove newline */
    
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(client_addr->sin_addr), client_ip, INET_ADDRSTRLEN);
    int client_port = ntohs(client_addr->sin_port);
    
    printf("[%s] Client %s:%d - %s - %s\n", time_str, client_ip, client_port, action, result);
}

/* Handle AVAILABLE command */
int handle_available(int client_fd) {
    char response[BUFFER_SIZE] = "AVAILABLE";
    char temp[32];
    int available_count = 0;
    
    /* CRITICAL SECTION: Check availability while holding mutex */
    pthread_mutex_lock(&seats_mutex);
    
    /* Collect all available seats */
    for (int i = 0; i < MAX_SEATS; i++) {
        if (seats[i].booked == 0) {
            snprintf(temp, sizeof(temp), " %d", seats[i].id);
            strcat(response, temp);
            available_count++;
        }
    }
    
    pthread_mutex_unlock(&seats_mutex);
    /* END CRITICAL SECTION */
    
    if (available_count == 0) {
        strcat(response, " NONE");
    }
    
    strcat(response, "\n");
    send(client_fd, response, strlen(response), 0);
    
    return 0;
}

/* Handle BOOK command with atomic multi-seat booking */
int handle_book(int client_fd, char* args, struct sockaddr_in* client_addr) {
    /* Make a copy of args since strtok modifies the string */
    char args_copy[BUFFER_SIZE];
    strncpy(args_copy, args, BUFFER_SIZE - 1);
    args_copy[BUFFER_SIZE - 1] = '\0';
    
    /* Parse the count (first number) and seat numbers */
    char* token = strtok(args_copy, " \t\n");
    if (token == NULL) {
        char* error = "FAIL invalid command format\n";
        send(client_fd, error, strlen(error), 0);
        log_request("BOOK", client_addr, "FAIL: invalid format");
        return -1;
    }
    
    int expected_count = atoi(token);
    if (expected_count <= 0 || expected_count > MAX_SEATS) {
        char* error = "FAIL invalid seat count\n";
        send(client_fd, error, strlen(error), 0);
        log_request("BOOK", client_addr, "FAIL: invalid count");
        return -1;
    }
    
    /* Parse the actual seat numbers (skip the count) */
    int seat_nums[MAX_SEATS];
    int num_seats = 0;
    token = strtok(NULL, " \t\n");
    
    while (token != NULL && num_seats < MAX_SEATS) {
        int seat_num = atoi(token);
        if (seat_num < 1 || seat_num > MAX_SEATS) {
            char error[BUFFER_SIZE];
            snprintf(error, sizeof(error), "FAIL invalid seat number %d\n", seat_num);
            send(client_fd, error, strlen(error), 0);
            log_request("BOOK", client_addr, "FAIL: invalid seat number");
            return -1;
        }
        seat_nums[num_seats++] = seat_num;
        token = strtok(NULL, " \t\n");
    }
    
    /* Validate that count matches number of seats provided */
    if (num_seats != expected_count) {
        char error[BUFFER_SIZE];
        snprintf(error, sizeof(error), "FAIL count mismatch: expected %d seats, got %d\n", expected_count, num_seats);
        send(client_fd, error, strlen(error), 0);
        log_request("BOOK", client_addr, "FAIL: count mismatch");
        return -1;
    }
    
    if (num_seats <= 0) {
        char* error = "FAIL no seats specified\n";
        send(client_fd, error, strlen(error), 0);
        log_request("BOOK", client_addr, "FAIL: no seats");
        return -1;
    }
    
    /* Check for duplicate seats in the request */
    for (int i = 0; i < num_seats; i++) {
        for (int j = i + 1; j < num_seats; j++) {
            if (seat_nums[i] == seat_nums[j]) {
                char error[BUFFER_SIZE];
                snprintf(error, sizeof(error), "FAIL duplicate seat %d in request\n", seat_nums[i]);
                send(client_fd, error, strlen(error), 0);
                log_request("BOOK", client_addr, "FAIL: duplicate seats");
                return -1;
            }
        }
    }
    
    /* 
     * CRITICAL SECTION: Atomic check-and-book operation
     * 
     * RACE CONDITION PREVENTION:
     * Without this mutex, the following could happen:
     *   1. Client A checks seat 5: available (booked=0)
     *   2. Client B checks seat 5: available (booked=0)  [BOTH see it as free!]
     *   3. Client A sets seat 5: booked=1
     *   4. Client B sets seat 5: booked=1  [DOUBLE BOOKING!]
     * 
     * With mutex:
     *   - Only one thread can enter this section at a time
     *   - Check and set operations are atomic
     *   - If Client A is checking/booking, Client B must wait
     *   - Client B will see seat 5 as already booked
     */
    pthread_mutex_lock(&seats_mutex);
    
    /* First pass: Check if ALL requested seats are available */
    int all_available = 1;
    int first_unavailable = -1;
    
    for (int i = 0; i < num_seats; i++) {
        int seat_idx = seat_nums[i] - 1; /* Convert seat number to array index */
        if (seats[seat_idx].booked != 0) {
            all_available = 0;
            first_unavailable = seat_nums[i];
            break;
        }
    }
    
    /* If all seats are available, book them all atomically */
    if (all_available) {
        char response[BUFFER_SIZE] = "OK BOOKED";
        char temp[32];
        
        for (int i = 0; i < num_seats; i++) {
            int seat_idx = seat_nums[i] - 1;
            seats[seat_idx].booked = 1;
            seats[seat_idx].booked_by = client_fd;
            
            snprintf(temp, sizeof(temp), " %d", seat_nums[i]);
            strcat(response, temp);
        }
        
        strcat(response, "\n");
        pthread_mutex_unlock(&seats_mutex);
        /* END CRITICAL SECTION */
        
        send(client_fd, response, strlen(response), 0);
        
        char log_msg[BUFFER_SIZE];
        snprintf(log_msg, sizeof(log_msg), "SUCCESS: booked %d seats", num_seats);
        log_request("BOOK", client_addr, log_msg);
        
        return 0;
    } else {
        /* At least one seat is unavailable - fail the entire request */
        pthread_mutex_unlock(&seats_mutex);
        /* END CRITICAL SECTION */
        
        char error[BUFFER_SIZE];
        snprintf(error, sizeof(error), "FAIL seat %d already booked\n", first_unavailable);
        send(client_fd, error, strlen(error), 0);
        
        char log_msg[BUFFER_SIZE];
        snprintf(log_msg, sizeof(log_msg), "FAIL: seat %d unavailable", first_unavailable);
        log_request("BOOK", client_addr, log_msg);
        
        return -1;
    }
}

/* Convert string to uppercase (in place) */
void to_upper(char* str) {
    for (int i = 0; str[i]; i++) {
        str[i] = toupper(str[i]);
    }
}

/* Process a command from the client (case-insensitive) */
int process_command(int client_fd, char* command, struct sockaddr_in* client_addr) {
    /* Remove trailing newline */
    command[strcspn(command, "\r\n")] = '\0';
    
    if (strlen(command) == 0) {
        return 0; /* Empty command, ignore */
    }
    
    /* Create uppercase copy for comparison (preserve original for BOOK args) */
    char cmd_upper[BUFFER_SIZE];
    strncpy(cmd_upper, command, BUFFER_SIZE - 1);
    cmd_upper[BUFFER_SIZE - 1] = '\0';
    to_upper(cmd_upper);
    
    /* Parse command (case-insensitive) */
    if (strncmp(cmd_upper, "AVAILABLE", 9) == 0) {
        return handle_available(client_fd);
    }
    else if (strncmp(cmd_upper, "BOOK", 4) == 0) {
        /* Extract arguments after "BOOK" (use original command to preserve case of args) */
        char* args = command + 4;
        while (*args == ' ' || *args == '\t') args++; /* Skip whitespace */
        return handle_book(client_fd, args, client_addr);
    }
    else if (strncmp(cmd_upper, "EXIT", 4) == 0) {
        log_request("EXIT", client_addr, "Client disconnecting");
        return 1; /* Signal to close connection */
    }
    else {
        char* error = "FAIL unknown command\n";
        send(client_fd, error, strlen(error), 0);
        log_request("UNKNOWN", client_addr, command);
        return 0;
    }
}

/* Thread function to handle a client connection */
void* handle_client(void* arg) {
    int client_fd = *(int*)arg;
    free(arg); /* Free the allocated memory for client_fd */
    
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    getpeername(client_fd, (struct sockaddr*)&client_addr, &addr_len);
    
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;
    
    log_request("CONNECT", &client_addr, "New client connected");
    
    /* Main loop: read commands from client */
    while ((bytes_read = recv(client_fd, buffer, BUFFER_SIZE - 1, 0)) > 0) {
        buffer[bytes_read] = '\0';
        
        /* Process each line (commands are line-oriented) */
        char* line = strtok(buffer, "\n");
        while (line != NULL) {
            int should_exit = process_command(client_fd, line, &client_addr);
            if (should_exit) {
                close(client_fd);
                pthread_exit(NULL);
            }
            line = strtok(NULL, "\n");
        }
    }
    
    /* Client disconnected */
    if (bytes_read == 0) {
        log_request("DISCONNECT", &client_addr, "Client closed connection");
    } else {
        log_request("ERROR", &client_addr, "Read error");
    }
    
    close(client_fd);
    pthread_exit(NULL);
}

int main(void) {
    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    pthread_t thread_id;
    
    /* Initialize seats */
    init_seats();
    printf("Server initialized with %d seats (1-%d)\n", MAX_SEATS, MAX_SEATS);
    
    /* Create socket */
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    
    /* Set socket options to allow reuse of address */
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt failed");
        exit(EXIT_FAILURE);
    }
    
    /* Configure server address */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    
    /* Bind socket */
    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }
    
    /* Listen for connections */
    if (listen(server_fd, MAX_CLIENTS) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }
    
    printf("Server listening on port %d...\n", PORT);
    printf("Waiting for client connections...\n\n");
    
    /* Main accept loop */
    while (1) {
        client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_addr_len);
        if (client_fd < 0) {
            perror("Accept failed");
            continue;
        }
        
        /* Allocate memory for client_fd to pass to thread */
        int* client_fd_ptr = malloc(sizeof(int));
        *client_fd_ptr = client_fd;
        
        /* Create thread to handle client */
        if (pthread_create(&thread_id, NULL, handle_client, client_fd_ptr) != 0) {
            perror("Thread creation failed");
            close(client_fd);
            free(client_fd_ptr);
            continue;
        }
        
        /* Detach thread so it cleans up automatically */
        pthread_detach(thread_id);
    }
    
    /* This code is never reached, but for completeness: */
    close(server_fd);
    pthread_mutex_destroy(&seats_mutex);
    return 0;
}

