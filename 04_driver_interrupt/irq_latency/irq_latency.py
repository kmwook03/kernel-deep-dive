from bcc import BPF
from ctypes import *
import sys

if len(sys.argv) < 2:
    print("Usage: sudo python3 irq_latency.py <IRQ>")
    exit(1)

TARGET_IRQ = int(sys.argv[1])

with open("irq_latency.c") as f:
    text = f.read()

text = text.replace("TARGET_IRQ", str(TARGET_IRQ))

b = BPF(text=text)

class Data(Structure):
    _fields_ = [
        ("duration", c_ulonglong)
    ]

print("IRQ Duration (ns)")

def print_event(cpu, data, size):
    event = cast(data, POINTER(Data)).contents
    print(event.duration)

b["events"].open_perf_buffer(print_event)

while True:
    try:
        b.perf_buffer_poll()
    except KeyboardInterrupt:
        exit()
