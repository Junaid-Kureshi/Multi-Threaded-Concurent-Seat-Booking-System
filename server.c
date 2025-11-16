/*
 * Multi-threaded Ticket Reservation Server
 * Protocol: AVAILABLE, BOOK n s1 s2..., CANCEL n s1 s2..., EXIT
 * Concurrency: seats_mutex protects seat array, log_mutex protects logging
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
#include <signal.h>

#define PORT 8080
#define MAX_SEATS 20
#define BUFFER_SIZE 1024
#define MAX_CLIENTS 100

struct seat {
    int id, booked, booked_by;
};

struct seat seats[MAX_SEATS];
pthread_mutex_t seats_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
volatile int server_fd_global = -1;

void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        write(STDERR_FILENO, "\n\nShutting down server...\n", 26);
        if (server_fd_global >= 0) close(server_fd_global);
        pthread_mutex_destroy(&seats_mutex);
        pthread_mutex_destroy(&log_mutex);
        exit(0);
    }
}

void init_seats(void) {
    for (int i = 0; i < MAX_SEATS; i++) {
        seats[i].id = i + 1;
        seats[i].booked = 0;
        seats[i].booked_by = -1;
    }
}

void log_request(const char* action, struct sockaddr_in* client_addr, const char* result) {
    pthread_mutex_lock(&log_mutex);
    time_t now = time(NULL);
    char* time_str = ctime(&now);
    time_str[strlen(time_str) - 1] = '\0';
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(client_addr->sin_addr), client_ip, INET_ADDRSTRLEN);
    printf("[%s] Client %s:%d - %s - %s\n", time_str, client_ip, ntohs(client_addr->sin_port), action, result);
    fflush(stdout);
    pthread_mutex_unlock(&log_mutex);
}

int handle_available(int client_fd) {
    char response[BUFFER_SIZE] = "AVAILABLE";
    char temp[32];
    int count = 0;
    
    pthread_mutex_lock(&seats_mutex);
    for (int i = 0; i < MAX_SEATS; i++) {
        if (seats[i].booked == 0) {
            snprintf(temp, sizeof(temp), " %d", seats[i].id);
            strcat(response, temp);
            count++;
        }
    }
    pthread_mutex_unlock(&seats_mutex);
    
    strcat(response, count ? "\n" : " NONE\n");
    send(client_fd, response, strlen(response), 0);
    return 0;
}

/* Parse seat numbers from command arguments */
int parse_seats(char* args, int* seat_nums, int* num_seats) {
    char args_copy[BUFFER_SIZE];
    strncpy(args_copy, args, BUFFER_SIZE - 1);
    args_copy[BUFFER_SIZE - 1] = '\0';
    
    char* token = strtok(args_copy, " \t\n");
    if (!token) return -1;
    
    int expected = atoi(token);
    if (expected <= 0 || expected > MAX_SEATS) return -1;
    
    *num_seats = 0;
    while ((token = strtok(NULL, " \t\n")) && *num_seats < MAX_SEATS) {
        int seat = atoi(token);
        if (seat < 1 || seat > MAX_SEATS) return -1;
        seat_nums[(*num_seats)++] = seat;
    }
    
    if (*num_seats != expected) return -1;
    
    /* Check duplicates */
    for (int i = 0; i < *num_seats; i++)
        for (int j = i + 1; j < *num_seats; j++)
            if (seat_nums[i] == seat_nums[j]) return -1;
    
    return 0;
}

int handle_cancel(int client_fd, char* args, struct sockaddr_in* client_addr) {
    int seat_nums[MAX_SEATS], num_seats;
    
    if (parse_seats(args, seat_nums, &num_seats) < 0) {
        send(client_fd, "FAIL invalid request\n", 21, 0);
        log_request("CANCEL", client_addr, "FAIL: invalid");
        return 0;
    }
    
    pthread_mutex_lock(&seats_mutex);
    
    /* Check all seats are booked and owned by this client */
    int all_ok = 1;
    int first_bad = -1;
    for (int i = 0; i < num_seats; i++) {
        int idx = seat_nums[i] - 1;
        if (seats[idx].booked == 0) {
            all_ok = 0;
            first_bad = seat_nums[i];
            break;
        }
        if (seats[idx].booked_by != client_fd) {
            all_ok = 0;
            first_bad = seat_nums[i];
            break;
        }
    }
    
    if (all_ok) {
        char response[BUFFER_SIZE] = "OK CANCELLED";
        for (int i = 0; i < num_seats; i++) {
            int idx = seat_nums[i] - 1;
            seats[idx].booked = 0;
            seats[idx].booked_by = -1;
            char temp[32];
            snprintf(temp, sizeof(temp), " %d", seat_nums[i]);
            strcat(response, temp);
        }
        strcat(response, "\n");
        pthread_mutex_unlock(&seats_mutex);
        send(client_fd, response, strlen(response), 0);
        log_request("CANCEL", client_addr, "SUCCESS");
        return 0;
    }
    
    pthread_mutex_unlock(&seats_mutex);
    char error[BUFFER_SIZE];
    snprintf(error, sizeof(error), "FAIL seat %d %s\n", first_bad,
             seats[first_bad - 1].booked == 0 ? "is not booked" : "was not booked by you");
    send(client_fd, error, strlen(error), 0);
    log_request("CANCEL", client_addr, "FAIL");
    return 0;
}

int handle_book(int client_fd, char* args, struct sockaddr_in* client_addr) {
    int seat_nums[MAX_SEATS], num_seats;
    
    if (parse_seats(args, seat_nums, &num_seats) < 0) {
        send(client_fd, "FAIL invalid request\n", 21, 0);
        log_request("BOOK", client_addr, "FAIL: invalid");
        return 0;
    }
    
    /* CRITICAL SECTION: Atomic check-and-book prevents double-booking */
    pthread_mutex_lock(&seats_mutex);
    
    int all_available = 1;
    int first_unavailable = -1;
    for (int i = 0; i < num_seats; i++) {
        int idx = seat_nums[i] - 1;
        if (seats[idx].booked != 0) {
            all_available = 0;
            first_unavailable = seat_nums[i];
            break;
        }
    }
    
    if (all_available) {
        char response[BUFFER_SIZE] = "OK BOOKED";
        for (int i = 0; i < num_seats; i++) {
            int idx = seat_nums[i] - 1;
            seats[idx].booked = 1;
            seats[idx].booked_by = client_fd;
            char temp[32];
            snprintf(temp, sizeof(temp), " %d", seat_nums[i]);
            strcat(response, temp);
        }
        strcat(response, "\n");
        pthread_mutex_unlock(&seats_mutex);
        send(client_fd, response, strlen(response), 0);
        log_request("BOOK", client_addr, "SUCCESS");
        return 0;
    }
    
    pthread_mutex_unlock(&seats_mutex);
    char error[BUFFER_SIZE];
    snprintf(error, sizeof(error), "FAIL seat %d already booked\n", first_unavailable);
    send(client_fd, error, strlen(error), 0);
    log_request("BOOK", client_addr, "FAIL");
    return 0;
}

void to_upper(char* str) {
    for (int i = 0; str[i]; i++) str[i] = toupper(str[i]);
}

int process_command(int client_fd, char* command, struct sockaddr_in* client_addr) {
    command[strcspn(command, "\r\n")] = '\0';
    if (strlen(command) == 0) return 0;
    
    char cmd_upper[BUFFER_SIZE];
    strncpy(cmd_upper, command, BUFFER_SIZE - 1);
    cmd_upper[BUFFER_SIZE - 1] = '\0';
    to_upper(cmd_upper);
    
    if (strncmp(cmd_upper, "AVAILABLE", 9) == 0) {
        return handle_available(client_fd);
    } else if (strncmp(cmd_upper, "BOOK", 4) == 0) {
        char* args = command + 4;
        while (*args == ' ' || *args == '\t') args++;
        return handle_book(client_fd, args, client_addr);
    } else if (strncmp(cmd_upper, "CANCEL", 6) == 0) {
        char* args = command + 6;
        while (*args == ' ' || *args == '\t') args++;
        return handle_cancel(client_fd, args, client_addr);
    } else if (strncmp(cmd_upper, "EXIT", 4) == 0) {
        log_request("EXIT", client_addr, "Disconnecting");
        return 1;
    } else {
        send(client_fd, "FAIL unknown command\n", 21, 0);
        log_request("UNKNOWN", client_addr, command);
        return 0;
    }
}

void* handle_client(void* arg) {
    int client_fd = *(int*)arg;
    free(arg);
    
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    getpeername(client_fd, (struct sockaddr*)&client_addr, &addr_len);
    
    char buffer[BUFFER_SIZE];
    log_request("CONNECT", &client_addr, "Connected");
    
    while (recv(client_fd, buffer, BUFFER_SIZE - 1, 0) > 0) {
        buffer[BUFFER_SIZE - 1] = '\0';
        char* line = strtok(buffer, "\n");
        while (line) {
            int result = process_command(client_fd, line, &client_addr);
            if (result == 1) {
                close(client_fd);
                pthread_exit(NULL);
            } else if (result == -1) {
                log_request("ERROR", &client_addr, "Send failed");
                close(client_fd);
                pthread_exit(NULL);
            }
            line = strtok(NULL, "\n");
        }
    }
    
    log_request("DISCONNECT", &client_addr, "Disconnected");
    close(client_fd);
    pthread_exit(NULL);
}

int main(void) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    init_seats();
    printf("Server initialized with %d seats. Press Ctrl+C to shutdown.\n\n", MAX_SEATS);
    
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }
    server_fd_global = server_fd;
    
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    
    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }
    
    if (listen(server_fd, MAX_CLIENTS) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }
    
    printf("Server listening on port %d...\n", PORT);
    
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &addr_len);
        
        if (client_fd < 0) continue;
        
        int* client_fd_ptr = malloc(sizeof(int));
        *client_fd_ptr = client_fd;
        
        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, handle_client, client_fd_ptr) == 0) {
            pthread_detach(thread_id);
        } else {
            close(client_fd);
            free(client_fd_ptr);
        }
    }
    
    return 0;
}
