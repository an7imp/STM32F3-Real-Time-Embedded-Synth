
import serial
from serial.tools import list_ports
import mido

print("Porte MIDI disponibili:")
for name in mido.get_input_names():
    print(name)

# Imposta la porta seriale preferita. Se non esiste e c'e una sola porta
# disponibile, viene selezionata automaticamente.
SERIAL_PORT = 'COM6'
BAUDRATE = 115200

serial_ports = [port.device for port in list_ports.comports()]
print("Porte seriali disponibili:", ", ".join(serial_ports) or "nessuna")

if SERIAL_PORT not in serial_ports:
    if len(serial_ports) == 1:
        SERIAL_PORT = serial_ports[0]
        print("Uso automaticamente:", SERIAL_PORT)
    else:
        raise RuntimeError(
            f"La porta {SERIAL_PORT} non e disponibile. "
            f"Imposta SERIAL_PORT scegliendo tra: {serial_ports}"
        )

# Inizializza la seriale
ser = serial.Serial(SERIAL_PORT, BAUDRATE)

# Porta MIDI
MIDI_PORT = 'LKMK3 MIDI 0'  
DEBUG_MIDI = False

# Legge il MIDI
with mido.open_input(MIDI_PORT) as port:
    print("In ascolto su:", MIDI_PORT)
    for msg in port:
        if msg.type in ['note_on', 'note_off', 'control_change']:
            data = msg.bytes()
            if DEBUG_MIDI:
                print("Invio:", data)
            ser.write(bytearray(data))  # Invio alla STM32
        
