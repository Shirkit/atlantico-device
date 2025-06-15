#!/usr/bin/env python3
"""
PlatformIO Custom Filter for ESP32 Log Colorization
Uses the existing log_colorizer.py logic
"""

import sys
import os

# Add the project root to Python path to import log_colorizer
project_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if project_root not in sys.path:
    sys.path.insert(0, project_root)

try:
    from log_colorizer import BufferedColorizer, load_config, colorize_line
    COLORIZER_AVAILABLE = True
except ImportError as e:
    print(f"Failed to import log_colorizer: {e}")
    # Fallback to basic implementation
    class BufferedColorizer:
        def __init__(self, config=None):
            pass
        def process_chunk(self, chunk):
            return chunk
        def flush(self):
            return ""
    def load_config():
        return {}
    COLORIZER_AVAILABLE = False

try:
    from platformio.public import DeviceMonitorFilterBase
except ImportError:
    try:
        from platformio.device.monitor.filters.base import DeviceMonitorFilterBase
    except ImportError:
        class DeviceMonitorFilterBase:
            def __init__(self, *args, **kwargs):
                pass

class Colorize(DeviceMonitorFilterBase):
    """ESP32 log colorizer filter using buffered colorization for chunked serial data"""
    
    NAME = "espcolorize"
    
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        if COLORIZER_AVAILABLE:
            print("🎨 ESP32 Colorize filter loaded with buffered processing!")
        else:
            print("⚠️  ESP32 Colorize filter loaded but colorization disabled")
        
        # Create buffered colorizer instance
        config = load_config()
        self.colorizer = BufferedColorizer(config)
    
    def rx(self, text):
        """Process received text from the device using buffered approach"""
        if not text:
            return text
        
        if not COLORIZER_AVAILABLE:
            return text
        
        try:
            # Process the chunk through the buffered colorizer
            # This will return complete colorized lines when available
            return self.colorizer.process_chunk(text)
        except Exception as e:
            # Fallback to original text if colorization fails
            print(f"Colorization error: {e}")
            return text
    
    def tx(self, text):
        """Process transmitted text to the device - pass through unchanged"""
        return text
    
    def __del__(self):
        """Flush any remaining buffered content when filter is destroyed"""
        if hasattr(self, 'colorizer') and COLORIZER_AVAILABLE:
            try:
                remaining = self.colorizer.flush()
                if remaining:
                    print(remaining, end='')
            except:
                pass
