# Multi-threaded Ticket Reservation System

A concurrent ticket reservation server and client implementation in C using POSIX sockets and pthreads. The system manages 20 seats and handles multiple simultaneous client connections with proper concurrency control to prevent double-booking.

## Features

- **Multi-threaded server**: One thread per client connection
- **Atomic multi-seat booking**: All-or-nothing transaction semantics
- **Concurrency control**: pthread mutex prevents race conditions
- **Real-time availability**: Instant seat status updates
- **Comprehensive logging**: Timestamped server logs for all operations

## Protocol

### Client Commands

- `AVAILABLE` - Query list of currently available seats
- `BOOK n s1 s2 ...` - Book `n` seats with numbers `s1, s2, ...` (atomic operation)
- `EXIT` - Disconnect gracefully

### Server Responses

- `AVAILABLE <seat_list>` - List of available seat numbers (or `NONE`)
- `OK BOOKED <seat_list>` - Successfully booked seats
- `FAIL <reason>` - Booking failed with reason

## Compilation

```bash
# Build both server and client
make

# Or compile manually:
gcc -pthread -Wall -Wextra -o server server.c
gcc -pthread -Wall -Wextra -o client client.c
```

## Running

### Terminal 1: Start the server
```bash
./server
```

The server will listen on port 8080 on all network interfaces (0.0.0.0:8080).

### Terminal 2: Start a client

**Connect to localhost (same machine):**
```bash
./client
# or explicitly:
./client 127.0.0.1
```

**Connect from your IP address (same machine or network):**
```bash
# Replace with your actual IP address
./client 10.183.102.46

# Or with a custom port:
./client 10.183.102.46 8080
```

**Connect from another machine on the network:**
```bash
# On the client machine, use the server's IP address
./client <server_ip_address>
```

**Find your IP address:**
```bash
# Linux
hostname -I
# or
ip addr show | grep "inet "

# The server will show client IPs in its logs when clients connect
```

Then enter commands interactively:
```
> AVAILABLE
> BOOK 2 5 10
> AVAILABLE
> EXIT
```

## Sample Session

```
# Client 1
> AVAILABLE
Server: AVAILABLE 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20

> BOOK 2 5 10
Server: OK BOOKED 5 10

> AVAILABLE
Server: AVAILABLE 1 2 3 4 6 7 8 9 11 12 13 14 15 16 17 18 19 20

> EXIT
```

```
# Client 2 (simultaneous)
> BOOK 1 5
Server: FAIL seat 5 already booked

> BOOK 3 1 2 3
Server: OK BOOKED 1 2 3
```

## Testing Concurrent Access

Use the provided `test.sh` script or run multiple clients manually:

```bash
# Terminal 1
./server

# Terminal 2
./client
# Enter: BOOK 1 10

# Terminal 3 (simultaneously)
./client
# Enter: BOOK 1 10
```

Only one client should succeed; the other will receive `FAIL seat 10 already booked`.

## Test Script

Run the automated test script:

```bash
chmod +x test.sh
./test.sh
```

This script:
1. Starts the server in the background
2. Sends concurrent booking requests for the same seat
3. Shows server logs demonstrating atomic operations
4. Cleans up processes

## Viva Talking Points

### 1. Where race conditions would occur without locks

**Scenario**: Two clients simultaneously try to book seat 5.

**Without mutex**:
```
Time    Client A                    Client B                    Seat 5 State
----    --------                    --------                    ------------
T1      Check: booked == 0? ✓       (waiting)                   booked=0
T2      (processing)                Check: booked == 0? ✓       booked=0
T3      Set: booked = 1             (processing)                booked=1
T4      (done)                      Set: booked = 1             booked=1
                                    
Result: BOTH clients think they booked seat 5! (DOUBLE BOOKING)
```

**With mutex**:
```
Time    Client A                    Client B                    Seat 5 State
----    --------                    --------                    ------------
T1      Lock mutex ✓                (blocked on mutex)          booked=0
T2      Check: booked == 0? ✓       (waiting)                   booked=0
T3      Set: booked = 1             (waiting)                   booked=1
T4      Unlock mutex                (waiting)                   booked=1
T5      (done)                      Lock mutex ✓                booked=1
T6      (done)                      Check: booked == 1? ✗       booked=1
T7      (done)                      Unlock mutex                booked=1
                                    
Result: Only Client A succeeds; Client B gets FAIL
```

### 2. Why check-then-set must be atomic

The **check-then-set** operation must be atomic because:

1. **Non-atomic check-then-set** allows a time window between checking and setting where another thread can interfere:
   ```c
   // WRONG: Not atomic
   if (seat[i].booked == 0) {        // Check
       // <-- Another thread can check here too!
       seat[i].booked = 1;           // Set
   }
   ```

2. **Atomic check-then-set** (with mutex) ensures no other thread can access the seat between check and set:
   ```c
   // CORRECT: Atomic
   pthread_mutex_lock(&seats_mutex);
   if (seat[i].booked == 0) {        // Check
       seat[i].booked = 1;           // Set
   }
   pthread_mutex_unlock(&seats_mutex);
   ```

3. **Multi-seat atomicity**: For `BOOK 2 5 10`, we check ALL seats first, then book ALL if available, or book NONE if any is unavailable. This prevents partial bookings.

### 3. How the design prevents double booking

1. **Single mutex protection**: All seat operations (read and write) are protected by `seats_mutex`.

2. **Critical section**: The entire check-and-book operation happens in one critical section:
   ```c
   pthread_mutex_lock(&seats_mutex);
   // Check all seats
   // If all available, book all
   // If any unavailable, book none
   pthread_mutex_unlock(&seats_mutex);
   ```

3. **Mutual exclusion**: Only one thread can modify seats at a time. Other threads must wait, ensuring sequential access to shared data.

4. **Atomic multi-seat booking**: The design ensures that either all requested seats are booked together, or none are. This prevents scenarios where seat 5 is booked but seat 10 fails, leaving an inconsistent state.

## Code Structure

- **`server.c`**: Main server with thread-per-client model
  - `init_seats()`: Initialize seat array
  - `handle_client()`: Thread function for each client
  - `process_command()`: Parse and route commands
  - `handle_available()`: Query available seats
  - `handle_book()`: Atomic multi-seat booking
  - `log_request()`: Timestamped logging

- **`client.c`**: Simple interactive client
  - Connects to server
  - Reads commands from stdin
  - Sends commands and displays responses

## Edge Cases Handled

- Invalid seat numbers (outside 1-20)
- Duplicate seats in booking request
- Zero seats requested
- Simultaneous booking of same seat
- Client disconnection handling
- Empty commands

## Extending the System

### Per-seat locks (for better concurrency)

Instead of a single global mutex, use an array of mutexes:

```c
pthread_mutex_t seat_mutexes[MAX_SEATS];

// Lock only the seats being accessed
for (int i = 0; i < num_seats; i++) {
    pthread_mutex_lock(&seat_mutexes[seat_nums[i] - 1]);
}
// ... check and book ...
for (int i = 0; i < num_seats; i++) {
    pthread_mutex_unlock(&seat_mutexes[seat_nums[i] - 1]);
}
```

**Trade-off**: More complex deadlock prevention needed (lock ordering).

### Scaling to N seats

The current design scales linearly. For very large N (thousands), consider:
- Per-seat locks (as above)
- Read-write locks if reads (AVAILABLE) are more frequent than writes (BOOK)
- Database backend for persistence

## License

Educational project for concurrent systems course.

