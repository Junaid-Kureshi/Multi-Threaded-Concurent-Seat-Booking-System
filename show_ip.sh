#!/bin/bash

# Script to show your IP address for connecting clients

echo "Your IP addresses:"
echo "=================="
echo ""

# Try different methods to get IP address
if command -v hostname &> /dev/null; then
    IP=$(hostname -I 2>/dev/null | awk '{print $1}')
    if [ ! -z "$IP" ]; then
        echo "Primary IP: $IP"
    fi
fi

# Alternative method
if [ -z "$IP" ]; then
    IP=$(ip addr show 2>/dev/null | grep -oP 'inet \K[\d.]+' | grep -v '127.0.0.1' | head -1)
    if [ ! -z "$IP" ]; then
        echo "Primary IP: $IP"
    fi
fi

# Show all IPs
echo ""
echo "All network interfaces:"
ip addr show 2>/dev/null | grep -E "^[0-9]+:|inet " | grep -v "127.0.0.1" || ifconfig 2>/dev/null | grep -E "inet " | grep -v "127.0.0.1"

echo ""
echo "To connect from another machine, use:"
if [ ! -z "$IP" ]; then
    echo "  ./client $IP"
    echo "  or"
    echo "  ./client $IP 8080"
else
    echo "  ./client <your_ip_address>"
fi
echo ""
echo "Make sure your firewall allows connections on port 8080!"

