import os
import socket
import mysql.connector
from datetime import datetime, timedelta
from dotenv import load_dotenv
import smtplib
from email.mime.text import MIMEText
from email.mime.multipart import MIMEMultipart
import threading
import queue
import time

load_dotenv()

DB_CONFIG = {
    'host': os.getenv('DB_HOST'),
    'user': os.getenv('DB_USER'),
    'password': os.getenv('DB_PASS'),
    'database': os.getenv('DB_NAME'),
}

# Thread-safe queue for asynchronous email processing
email_queue = queue.Queue()

def email_worker():
    """Background worker to handle emails without blocking the main socket loop."""
    while True:
        try:
            item = email_queue.get()
            if item is None:
                break
            
            subject, body = item
            smtp_server = "smtp.gmail.com"
            smtp_port = 587
            sender_email = os.getenv('SENDER_GMAIL')
            receiver_email = os.getenv('RECEIVER_GMAIL')
            app_password = os.getenv('APP_PASS_GMAIL')  

            msg = MIMEMultipart()
            msg['From'] = sender_email
            msg['To'] = receiver_email
            msg['Subject'] = subject
            msg.attach(MIMEText(body, 'plain'))

            try:
                with smtplib.SMTP(smtp_server, smtp_port, timeout=10) as server:
                    server.starttls()
                    server.login(sender_email, app_password)
                    server.send_message(msg)
                    print("Email sent successfully.", flush=True)
            except Exception as e:
                print(f"Error sending email: {e}", flush=True)
            finally:
                email_queue.task_done()
        except Exception as e:
            print(f"Email worker unexpected error: {e}", flush=True)

def send_notification_async(subject, body):
    """Enqueues emails to prevent blocking network execution."""
    email_queue.put((subject, body))

def handle_mailbox_data(hex_str, timestamp):
    conn = None
    try:
        if len(hex_str) != 12:
            print(f"Invalid payload length: {hex_str}", flush=True)
            return

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
        
        if trigger == 2:
            send_notification_async(subject="Mailbox Opened", body="The mailbox door sensor say it was opened.  Love self.")
        elif trigger == 3:
            send_notification_async(subject="Mailbox reset", body="The mailbox door sensor say it was reset.  Love self.")
        elif trigger == 0:
            send_notification_async(subject="Mailbox power up boot", body="The mailbox power was reset.  Love self.")
            
    except ValueError as ve:
        print(f"Mailbox Hex Parse Value Error: {ve} for data: {hex_str}", flush=True)
    except Exception as e:
        print(f"Mailbox Parse Error: {e}", flush=True)
    finally:
        if conn and conn.is_connected():
            conn.close()

def run_collector():
    # Start the background email processor
    threading.Thread(target=email_worker, daemon=True).start()

    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    
    # Prevent infinite blocking on the accept loop
    server.settimeout(60.0)
    server.bind(('0.0.0.0', int(os.getenv('COLLECTOR_PORT'))))
    server.listen(5)
    print(f"Mailbox Collector listening...", flush=True)
    
    while True:
        try:
            try:
                conn, addr = server.accept()
            except socket.timeout:
                continue

            lines = []
            remainder_buffer = ""
            
            try:
                conn.settimeout(12.0)
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
                            
                            # Wrap sendall in a brief timeout to protect against dropped signals
                            conn.sendall(b"ACK\n")
                            
            except (socket.timeout, socket.error):
                pass
            finally:
                if remainder_buffer.strip():
                    lines.append(remainder_buffer.strip())
                try:
                    conn.shutdown(socket.SHUT_WR)
                    conn.close()
                except Exception:
                    pass
            
            if lines:
                current_time = datetime.now()
                total_lines = len(lines)
                
                for index, line in enumerate(lines):
                    minutes_back = (total_lines - 1 - index) * 10
                    line_timestamp = current_time - timedelta(minutes=minutes_back)
                    handle_mailbox_data(line, line_timestamp)

        except Exception as e:
            print(f"Collector Error: {e}", flush=True)
            time.sleep(2)

if __name__ == "__main__":
    run_collector()