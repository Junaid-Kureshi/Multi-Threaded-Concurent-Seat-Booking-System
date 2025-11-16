#!/bin/bash

# Test script for Ticket Reservation System
# Demonstrates concurrent booking scenarios

PORT=8080
SERVER="./server"
CLIENT="./client"

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${YELLOW}=== Ticket Reservation System Test Script ===${NC}\n"

# Check if server and client binaries exist
if [ ! -f "$SERVER" ]; then
    echo -e "${RED}Error: $SERVER not found. Run 'make' first.${NC}"
    exit 1
fi

if [ ! -f "$CLIENT" ]; then
    echo -e "${RED}Error: $CLIENT not found. Run 'make' first.${NC}"
    exit 1
fi

# Cleanup function
cleanup() {
    echo -e "\n${YELLOW}Cleaning up...${NC}"
    pkill -f "$SERVER" 2>/dev/null
    pkill -f "$CLIENT" 2>/dev/null
    sleep 1
    echo -e "${GREEN}Cleanup complete.${NC}"
}

# Trap Ctrl+C and cleanup
trap cleanup EXIT INT TERM

# Start server in background
echo -e "${GREEN}Starting server...${NC}"
$SERVER > server.log 2>&1 &
SERVER_PID=$!
sleep 2

# Check if server started successfully
if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo -e "${RED}Error: Server failed to start. Check server.log${NC}"
    exit 1
fi

echo -e "${GREEN}Server started (PID: $SERVER_PID)${NC}\n"

# Test 1: Basic availability check
echo -e "${YELLOW}Test 1: Check available seats${NC}"
echo "AVAILABLE" | timeout 2 $CLIENT 2>/dev/null | grep "Server:"
echo ""

# Test 2: Book a single seat
echo -e "${YELLOW}Test 2: Book seat 10${NC}"
echo -e "BOOK 1 10\nEXIT" | timeout 2 $CLIENT 2>/dev/null | grep "Server:"
echo ""

# Test 3: Check availability after booking
echo -e "${YELLOW}Test 3: Check availability (seat 10 should be gone)${NC}"
echo "AVAILABLE" | timeout 2 $CLIENT 2>/dev/null | grep "Server:"
echo ""

# Test 4: Try to book already booked seat
echo -e "${YELLOW}Test 4: Try to book already booked seat 10${NC}"
echo -e "BOOK 1 10\nEXIT" | timeout 2 $CLIENT 2>/dev/null | grep "Server:"
echo ""

# Test 5: Multi-seat booking
echo -e "${YELLOW}Test 5: Book multiple seats (5, 15, 20)${NC}"
echo -e "BOOK 3 5 15 20\nEXIT" | timeout 2 $CLIENT 2>/dev/null | grep "Server:"
echo ""

# Test 6: Concurrent booking of same seat (RACE CONDITION TEST)
echo -e "${YELLOW}Test 6: Concurrent booking of seat 7 (RACE CONDITION TEST)${NC}"
echo -e "${YELLOW}Two clients will try to book seat 7 simultaneously...${NC}\n"

# Reset seat 7 first (if we had a reset command, but we don't, so we'll use a different seat)
# Instead, let's test with seat 3
echo -e "${YELLOW}Testing concurrent access to seat 3...${NC}"

# Send two booking requests almost simultaneously
(echo -e "BOOK 1 3\nEXIT" | timeout 2 $CLIENT 2>/dev/null) &
CLIENT1_PID=$!
(echo -e "BOOK 1 3\nEXIT" | timeout 2 $CLIENT 2>/dev/null) &
CLIENT2_PID=$!

wait $CLIENT1_PID
wait $CLIENT2_PID

echo -e "${GREEN}Concurrent booking test completed.${NC}"
echo -e "${YELLOW}Check server.log to see which client succeeded.${NC}\n"

# Test 7: Atomic multi-seat booking (partial failure)
echo -e "${YELLOW}Test 7: Try to book seats 3 (taken) and 4 (available) - should fail atomically${NC}"
echo -e "BOOK 2 3 4\nEXIT" | timeout 2 $CLIENT 2>/dev/null | grep "Server:"
echo ""

# Test 8: Invalid seat number
echo -e "${YELLOW}Test 8: Try to book invalid seat (25)${NC}"
echo -e "BOOK 1 25\nEXIT" | timeout 2 $CLIENT 2>/dev/null | grep "Server:"
echo ""

# Test 9: Duplicate seats in request
echo -e "${YELLOW}Test 9: Try to book duplicate seats (1, 1)${NC}"
echo -e "BOOK 2 1 1\nEXIT" | timeout 2 $CLIENT 2>/dev/null | grep "Server:"
echo ""

# Show server log
echo -e "${YELLOW}=== Server Log (last 20 lines) ===${NC}"
tail -20 server.log
echo ""

echo -e "${GREEN}=== All tests completed ===${NC}"
echo -e "${YELLOW}Server is still running. Press Ctrl+C to stop.${NC}"
echo -e "${YELLOW}Or run: kill $SERVER_PID${NC}\n"

# Keep server running for manual testing
echo -e "${YELLOW}Server will continue running for manual testing...${NC}"
echo -e "${YELLOW}You can now run: ./client${NC}"
echo -e "${YELLOW}Press Ctrl+C to stop the server.${NC}\n"

# Wait for user interrupt
wait $SERVER_PID

