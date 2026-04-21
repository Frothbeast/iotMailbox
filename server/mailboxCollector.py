import os
import socket
import mysql.connector
from datetime import datetime
from dotenv import load_dotenv

load_dotenv()

DB_CONFIG = {
    'host': os.getenv('DB_HOST'),
    'user': os.getenv('DB_USER'),
    'password': os.getenv('DB_PASS'),
    'database': os.getenv('DB_NAME'),
}

def handle_mailbox_data(hex_str):
    try:
        # Hex format: ID(2) Trigger(2) Temp*100(4) RSSI(4)
        dev_id = int(hex_str[0:2], 16)
        trigger = int(hex_str[2:4], 16)
        temp_raw = int(hex_str[4:8], 16)
        rssi_raw = int(hex_str[8:12], 16)
        
        temp_c = temp_raw / 100.0
        # Convert to signed 16-bit if RSSI was sent as such, else keep as is
        rssi = rssi_raw if rssi_raw < 32768 else rssi_raw - 65536

        conn = mysql.connector.connect(**DB_CONFIG)
        cursor = conn.cursor()
        query = "INSERT INTO mailboxData (datetime, deviceID, triggerEvent, temp, rssi) VALUES (%s, %s, %s, %s, %s)"
        cursor.execute(query, (datetime.now(), dev_id, trigger, temp_c, rssi))
        conn.commit()
        cursor.close()
        conn.close()
    except Exception as e:
        print(f"Mailbox Parse Error: {e}")

def run_collector():
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.bind(('0.0.0.0', int(os.getenv('COLLECTOR_PORT', 5001))))
    server.listen(5)
    while True:
        conn, addr = server.accept()
        data = conn.recv(1024)
        if data:
            handle_mailbox_data(data.decode('ascii').strip())
            conn.sendall(b"ACK")
        conn.close()

if __name__ == "__main__":
    run_collector()