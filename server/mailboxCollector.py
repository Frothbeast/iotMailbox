import os
import socket
import mysql.connector
from datetime import datetime, timedelta
from dotenv import load_dotenv

load_dotenv()

DB_CONFIG = {
    'host': os.getenv('DB_HOST'),
    'user': os.getenv('DB_USER'),
    'password': os.getenv('DB_PASS'),
    'database': os.getenv('DB_NAME'),
}

def handle_mailbox_data(hex_str, timestamp):
    conn = None
    try:
        # Hex format: ID(2) Trigger(2) Temp*100(4) RSSI(4)
        dev_id = int(hex_str[0:2], 16)
        trigger = int(hex_str[2:4], 16)
        temp_raw = int(hex_str[4:8], 16)
        rssi_raw = int(hex_str[8:12], 16)
        
        temp_c = temp_raw / 100.0
        rssi = rssi_raw if rssi_raw < 32768 else rssi_raw - 65536

        conn = mysql.connector.connect(**DB_CONFIG)
        cursor = conn.cursor()
        query = "INSERT INTO mailboxData (datetime, deviceID, triggerEvent, temp, rssi) VALUES (%s, %s, %s, %s, %s)"
        cursor.execute(query, (timestamp, dev_id, trigger, temp_c, rssi))
        conn.commit()
        cursor.close()
    except Exception as e:
        print(f"Mailbox Parse Error: {e}")
    finally:
        if conn and conn.is_connected():
            conn.close()

def run_collector():
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.bind(('0.0.0.0', int(os.getenv('COLLECTOR_PORT'))))
    server.listen(5)
    
    while True:
        conn, addr = server.accept()
        conn.settimeout(5.0)
        try:
            lines = []
            while True:
                data = conn.recv(1024)
                if not data:
                    break
                
                # Split incoming stream by newlines to handle separate data entries
                chunk = data.decode('ascii').strip().split('\n')
                for line in chunk:
                    clean_line = line.strip()
                    if clean_line:
                        lines.append(clean_line)
            
            if lines:
                current_time = datetime.now()
                total_lines = len(lines)
                for index, line in enumerate(lines):
                    # Calculate negative offset from the current time based on index positions
                    minutes_back = (total_lines - 1 - index) * 10
                    line_timestamp = current_time - timedelta(minutes=minutes_back)
                    handle_mailbox_data(line, line_timestamp)
                    
                conn.sendall(b"ACK\n")
        except socket.timeout:
            pass
        finally:
            conn.close()

if __name__ == "__main__":
    run_collector()