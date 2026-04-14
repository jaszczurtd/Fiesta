#!/usr/bin/env python3
"""
Serial Monitor dla Arduino-Pico w VS Code.

Używa pyserial lub bezpośrednio termios do obsługi portu szeregowego.
Poprawnie ustawia DTR (wymagane przez USB CDC na Pico).
Ctrl+C kończy sesję.
"""

import sys
import os
import json
import signal
import time

def find_settings():
    """Szukaj settings.json w katalogu projektu."""
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_dir = os.path.dirname(script_dir)
    settings_path = os.path.join(project_dir, ".vscode", "settings.json")
    if os.path.isfile(settings_path):
        with open(settings_path) as f:
            return json.load(f)
    return {}

def monitor_with_pyserial(port, baud):
    """Monitor z pyserial — preferowana metoda."""
    import serial
    
    print(f"  Metoda: pyserial")
    print(f"  Łączenie z {port}...")
    print(f"  \033[1;33mCtrl+C\033[0m aby zakończyć")
    print("-" * 40)
    
    ser = serial.Serial(
        port=port,
        baudrate=baud,
        bytesize=serial.EIGHTBITS,
        parity=serial.PARITY_NONE,
        stopbits=serial.STOPBITS_ONE,
        timeout=0.1,
        dsrdtr=True,
        rtscts=False,
    )
    
    # Ustaw DTR — kluczowe dla USB CDC na Pico
    ser.dtr = True
    ser.rts = True
    
    try:
        while True:
            data = ser.read(ser.in_waiting or 1)
            if data:
                sys.stdout.buffer.write(data)
                sys.stdout.buffer.flush()
    except KeyboardInterrupt:
        pass
    finally:
        ser.close()
        print("\n\033[0;36mRozłączono.\033[0m")

def monitor_with_termios(port, baud):
    """Fallback monitor z termios — bez pyserial."""
    import termios
    import fcntl
    import struct
    
    print(f"  Metoda: termios (fallback)")
    print(f"  Łączenie z {port}...")
    print(f"  \033[1;33mCtrl+C\033[0m aby zakończyć")
    print("-" * 40)
    
    # Mapowanie baud rate
    baud_map = {
        9600: termios.B9600,
        19200: termios.B19200,
        38400: termios.B38400,
        57600: termios.B57600,
        115200: termios.B115200,
        230400: termios.B230400,
    }
    
    baud_const = baud_map.get(baud, termios.B115200)
    
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    
    try:
        # Konfiguracja portu
        attrs = termios.tcgetattr(fd)
        attrs[0] = 0        # iflag — brak przetwarzania wejścia
        attrs[1] = 0        # oflag — brak przetwarzania wyjścia
        attrs[2] = (termios.CS8 | termios.CREAD | termios.CLOCAL | termios.HUPCL)
        attrs[3] = 0        # lflag — raw mode
        attrs[4] = baud_const  # ispeed
        attrs[5] = baud_const  # ospeed
        attrs[6][termios.VMIN] = 0
        attrs[6][termios.VTIME] = 1  # 100ms timeout
        termios.tcsetattr(fd, termios.TCSANOW, attrs)
        
        # Ustaw DTR i RTS
        TIOCMBIS = 0x5416
        TIOCM_DTR = 0x002
        TIOCM_RTS = 0x004
        fcntl.ioctl(fd, TIOCMBIS, struct.pack('I', TIOCM_DTR | TIOCM_RTS))
        
        # Przełącz na blocking z timeoutem
        import select
        
        while True:
            ready, _, _ = select.select([fd], [], [], 0.1)
            if ready:
                data = os.read(fd, 4096)
                if data:
                    sys.stdout.buffer.write(data)
                    sys.stdout.buffer.flush()
    except KeyboardInterrupt:
        pass
    finally:
        os.close(fd)
        print("\n\033[0;36mRozłączono.\033[0m")

def main():
    settings = find_settings()
    port = settings.get("arduino.uploadPort", "/dev/ttyACM0")
    baud = 115200
    
    print(f"\033[0;36mSerial Monitor\033[0m")
    print(f"  Port: \033[0;32m{port}\033[0m")
    print(f"  Baud: \033[0;32m{baud}\033[0m")
    print()
    
    # Sprawdź czy port istnieje
    if not os.path.exists(port):
        print(f"\033[0;31mPort {port} nie istnieje\033[0m")
        print()
        print("Dostępne porty:")
        import glob
        ports = glob.glob("/dev/ttyACM*") + glob.glob("/dev/ttyUSB*")
        for p in sorted(ports):
            print(f"  {p}")
        if not ports:
            print("  (brak — podłącz płytkę)")
        sys.exit(1)
    
    # Spróbuj pyserial, fallback na termios
    try:
        import serial
        monitor_with_pyserial(port, baud)
    except ImportError:
        print("  \033[1;33mpyserial nie zainstalowany — używam termios\033[0m")
        print("  (Tip: pip install pyserial --break-system-packages)")
        print()
        monitor_with_termios(port, baud)

if __name__ == "__main__":
    # Obsłuż Ctrl+C czysto
    signal.signal(signal.SIGINT, lambda s, f: sys.exit(0))
    main()
