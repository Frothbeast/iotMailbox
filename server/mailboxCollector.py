import os
import socket
import mysql.connector
from datetime import datetime, timedelta
from dotenv import load_dotenv
import smtplib
from email.mime.text import MIMEText
from email.mime.multipart import MIMEMultipart
load_dotenv()

DB_CONFIG = {
    'host': os.getenv('DB_HOST'),
    'user': os.getenv('DB_USER'),
    'password': os.getenv('DB_PASS'),
    'database': os.getenv('DB_NAME'),
}

def send_notification(subject, body):
    # Configuration
    smtp_server = "smtp.gmail.com"
    smtp_port = 587
    sender_email = os.getenv('SENDER_GMAIL')
    receiver_email = os.getenv('RECEIVER_GMAIL')
    app_password = os.getenv('APP_PASS_GMAIL')  

    # Create message headers and body
    msg = MIMEMultipart()
    msg['From'] = sender_email
    msg['To'] = receiver_email
    msg['Subject'] = subject
    msg.attach(MIMEText(body, 'plain'))

    try:
        # Establish a secure connection
        server = smtplib.SMTP(smtp_server, smtp_port)
        server.starttls()  # Upgrade the connection to secure TLS
        
        # Authenticate and send
        server.login(sender_email, app_password)
        server.send_message(msg)
        print("Email sent successfully.")
        
    except Exception as e:
        print(f"Error sending email: {e}")
        
    finally:
        server.quit()

def handle_mailbox_data(hex_str, timestamp):
    conn = None
    try:
        # Explicit 12-character slicing matching the ESP32 sprintf layout:
        # %02X (0:2) %02X (2:4) %04X (4:8) %04X (8:12)
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
        if trigger==2:
            send_notification(
                subject="Mailbox Opened", 
                body="The mailbox door sensor say it was opened.  Love self."
            )
        if trigger==3:
            send_notification(
                subject="Mailbox reset", 
                body="The mailbox door sensor say it was reset.  Love self."
            )
        if trigger==0:
            send_notification(
                subject="Mailbox power up boot", 
                body="The mailbox power was reset.  Love self."
            )
    except Exception as e:
        print(f"Mailbox Parse Error: {e}")
    finally:
        if conn and conn.is_connected():
            conn.close()

def run_collector():
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(('0.0.0.0', int(os.getenv('COLLECTOR_PORT'))))
    server.listen(5)
    
    while True:
        conn, addr = server.accept()
        lines = []
        try:
            conn.settimeout(12.0)
            remainder_buffer = ""
            
            while True:
                data = conn.recv(1024)
                if not data:
                    break
                
                remainder_buffer += data.decode('ascii', errors='ignore')
                
                while "\n" in remainder_buffer:
                    line_segment, remainder_buffer = remainder_buffer.split("\n", 1)
                    clean_line = line_segment.strip()
                    if clean_line:
                        lines.append(clean_line)
                        conn.sendall(b"ACK\n")
                        
        except (socket.timeout, socket.error):
            pass
        finally:
            if remainder_buffer.strip():
                lines.append(remainder_buffer.strip())
            
            conn.close()
            
            if lines:
                current_time = datetime.now()
                total_lines = len(lines)
                
                for index, line in enumerate(lines):
                    # Only parse lines that match the strict 12-character hex payload requirement
                    if len(line) == 12:
                        minutes_back = (total_lines - 1 - index) * 10
                        line_timestamp = current_time - timedelta(minutes=minutes_back)
                        handle_mailbox_data(line, line_timestamp)

if __name__ == "__main__":
    run_collector()