#!/usr/bin/env python3
"""
Simple ESP32 Log Colorizer - Post-processes PlatformIO monitor output
"""

import sys
import re
import json
import os

# ANSI color codes
class Colors:
    RED = '\033[31m'
    YELLOW = '\033[33m'
    GREEN = '\033[32m'
    CYAN = '\033[36m'
    MAGENTA = '\033[35m'
    WHITE = '\033[37m'
    DARK_GRAY = '\033[90m'
    RESET = '\033[0m'

class BufferedColorizer:
    """
    A buffered colorizer that handles chunked serial data.
    Accumulates text until complete lines are received, then applies colorization.
    """
    
    def __init__(self, config=None):
        self.config = config or load_config()
        self.buffer = ""
    
    def process_chunk(self, chunk):
        """
        Process a chunk of data, which may contain partial lines.
        Returns any complete colorized lines that are ready for output.
        """
        if not chunk:
            return ""
        
        # Add chunk to buffer
        self.buffer += chunk
        
        # Split buffer into lines
        lines = self.buffer.split('\n')
        
        # Keep the last incomplete line in buffer
        self.buffer = lines[-1] if lines else ""
        
        # Process complete lines (all but the last)
        output = ""
        for line in lines[:-1]:
            # Add newline back since split removes it
            complete_line = line + '\n'
            
            # Skip monitor control lines
            if complete_line.startswith('---'):
                output += complete_line
                continue
            
            # Colorize the complete line
            colorized = colorize_line(complete_line, self.config)
            output += colorized
        
        return output
    
    def flush(self):
        """
        Flush any remaining buffered content.
        Useful when connection is closed or manual flush is needed.
        """
        if self.buffer:
            # Process any remaining content as a complete line
            remaining = self.buffer
            self.buffer = ""
            
            if remaining.startswith('---'):
                return remaining
            
            return colorize_line(remaining, self.config)
        return ""
    
    def clear_buffer(self):
        """Clear the internal buffer."""
        self.buffer = ""

def load_config():
    """Load color configuration."""
    config_path = os.path.join(os.getcwd(), '.platformio', 'monitor_colors.json')
    
    default_config = {
        "log_levels": {
            "ERROR": Colors.RED,
            "WARN": Colors.YELLOW,
            "INFO": Colors.GREEN,
            "DEBUG": Colors.CYAN,
            "TRACE": Colors.MAGENTA
        },
        "components": {
            "timestamp": Colors.WHITE,
            "filename": Colors.DARK_GRAY,
            "function": Colors.DARK_GRAY,
            "message": Colors.WHITE,
            "brackets": Colors.DARK_GRAY
        }
    }
    
    try:
        if os.path.exists(config_path):
            with open(config_path, 'r') as f:
                loaded = json.load(f)
                # Use theme if specified
                if 'themes' in loaded and 'theme' in loaded:
                    theme_name = loaded['theme']
                    if theme_name in loaded['themes']:
                        theme = loaded['themes'][theme_name]
                        if 'log_levels' in theme:
                            default_config['log_levels'].update(theme['log_levels'])
                        if 'components' in theme:
                            default_config['components'].update(theme['components'])
                return default_config
    except:
        pass
    
    return default_config

def colorize_line(line, config):
    """Colorize a single line of ESP32 log output."""
    # Preserve original line ending
    original_line = line
    stripped_line = line.strip()
    line_ending = line[len(stripped_line):]
    
    # Single log line processing first (most common case)
    return colorize_single_line(stripped_line, config) + line_ending

def colorize_single_line(line, config):
    """Colorize a single, clean log line."""
    if not line.strip():
        return line
        
    stripped_line = line.strip()
    
    # Pattern to match ESP32 Logger format: [timestamp][LEVEL][file:line][function] message
    # Main pattern: [123][INFO ][file.cpp:45][function] message
    pattern = r'(\[\d+\])(\[(\w+)\s*\])(\[([^:]+):(\d+)\])(\[([^\]]+)\])\s*(.*)'
    match = re.match(pattern, stripped_line)
    
    if match:
        timestamp, level_bracket, level, file_bracket, filename, line_num, func_bracket, function, message = match.groups()
        
        # Get colors
        level_color = config['log_levels'].get(level.strip(), Colors.WHITE)
        timestamp_color = config['components']['timestamp']
        file_color = config['components']['filename']
        func_color = config['components']['function']
        bracket_color = config['components']['brackets']
        reset = Colors.RESET
        
        # Build colorized line
        colored_timestamp = f"{bracket_color}[{timestamp_color}{timestamp[1:-1]}{bracket_color}]{reset}"
        colored_level = f"{bracket_color}[{level_color}{level}{bracket_color}]{reset}"
        colored_file = f"{bracket_color}[{file_color}{filename}:{line_num}{bracket_color}]{reset}"
        colored_func = f"{bracket_color}[{func_color}{function}{bracket_color}]{reset}"
        colored_message = f"{level_color}{message}{reset}"
        
        return f"{colored_timestamp}{colored_level}{colored_file}{colored_func} {colored_message}"
    
    # Try pattern with spaces: [timestamp] [LEVEL]...
    spaced_pattern = r'(\[\d+\])\s+(\[(\w+)\s*\])\s*(\[([^:]+):(\d+)\])(\[([^\]]+)\])\s*(.*)'
    spaced_match = re.match(spaced_pattern, stripped_line)
    if spaced_match:
        timestamp, level_bracket, level, file_bracket, filename, line_num, func_bracket, function, message = spaced_match.groups()
        
        # Get colors
        level_color = config['log_levels'].get(level.strip(), Colors.WHITE)
        timestamp_color = config['components']['timestamp']
        file_color = config['components']['filename']
        func_color = config['components']['function']
        bracket_color = config['components']['brackets']
        reset = Colors.RESET
        
        # Build colorized line
        colored_timestamp = f"{bracket_color}[{timestamp_color}{timestamp[1:-1]}{bracket_color}]{reset}"
        colored_level = f"{bracket_color}[{level_color}{level}{bracket_color}]{reset}"
        colored_file = f"{bracket_color}[{file_color}{filename}:{line_num}{bracket_color}]{reset}"
        colored_func = f"{bracket_color}[{func_color}{function}{bracket_color}]{reset}"
        colored_message = f"{level_color}{message}{reset}"
        
        return f"{colored_timestamp} {colored_level}{colored_file}{colored_func} {colored_message}"
    
    # Pattern for simple level messages like "[INFO] message"
    simple_pattern = r'(\[(\w+)\])\s*(.*)'
    simple_match = re.match(simple_pattern, stripped_line)
    if simple_match:
        level_bracket, level, message = simple_match.groups()
        level_color = config['log_levels'].get(level, Colors.WHITE)
        bracket_color = config['components']['brackets']
        reset = Colors.RESET
        return f"{bracket_color}[{level_color}{level}{bracket_color}]{reset} {level_color}{message}{reset}"
    
    # Return original line if no pattern matches
    return stripped_line

def main():
    """Main function - reads from stdin and outputs colorized lines."""
    config = load_config()
    colorizer = BufferedColorizer(config)
    
    try:
        while True:
            chunk = sys.stdin.read(1024)  # Read chunks of data
            if not chunk:
                break
            
            output = colorizer.process_chunk(chunk)
            if output:
                print(output, end='')
                sys.stdout.flush()
        
        # Flush remaining buffer
        remaining_output = colorizer.flush()
        if remaining_output:
            print(remaining_output, end='')
            sys.stdout.flush()
    except KeyboardInterrupt:
        pass

if __name__ == "__main__":
    main()
