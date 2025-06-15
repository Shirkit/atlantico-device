#!/bin/bash
# ESP32 Colorized Monitor Wrapper
# This script runs PlatformIO monitor with colorized output

# Check if we're in a PlatformIO project
if [ ! -f "platformio.ini" ]; then
    echo "Error: Not in a PlatformIO project directory"
    exit 1
fi

# Default environment
ENV="esp0"

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -e|--environment)
            ENV="$2"
            shift 2
            ;;
        --test)
            echo "🧪 Test mode: Reading from stdin and colorizing..."
            python3 log_colorizer.py
            exit 0
            ;;
        -h|--help)
            echo "Usage: $0 [-e|--environment ENV] [--test]"
            echo "  -e, --environment ENV    PlatformIO environment (default: esp0)"
            echo "  --test                   Test mode: read from stdin"
            echo "  -h, --help              Show this help message"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

echo "Starting colorized ESP32 monitor for environment: $ENV"
echo "Press Ctrl+C to exit"
echo "============================================================"

# Run PlatformIO monitor and pipe through our colorizer
pio device monitor -e "$ENV" | python3 log_colorizer.py
