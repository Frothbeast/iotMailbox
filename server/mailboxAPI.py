from flask import Flask, request, jsonify, send_from_directory
import mysql.connector
import json
from datetime import datetime
import time
import os
import requests
import urllib3
from flask_cors import CORS
from dotenv import load_dotenv
import sys
from decimal import Decimal
import pytz

# Setup static directory for React serving
static_dir = os.environ.get('STATIC_FOLDER', '/app/client/build')
app = Flask(__name__, static_folder=static_dir, static_url_path='/')

CORS(app)
load_dotenv()

# Environment Variables
CL1P_TOKEN = os.getenv('CL1P_TOKEN')
CL1P_URL = os.getenv('CL1P_URL')
LOCATION = os.getenv('LOCATION')

DB_CONFIG = {
    'host': os.getenv('DB_HOST'),
    'user': os.getenv('DB_USER'),
    'password': os.getenv('DB_PASS'),
    'database': os.getenv('DB_NAME')
}

# --- Shared Utility Functions ---

def datetime_handler(x):
    """Handles JSON serialization for datetime and Decimal types[cite: 1]."""
    if isinstance(x, datetime):
        return x.isoformat()
    if isinstance(x, Decimal):
        return float(x)
    raise TypeError(f"Unknown type: {type(x)}")

def get_db_connection():
    """Robust connection handler with retries[cite: 1]."""
    retries = 5
    while retries > 0:
        try:
            conn = mysql.connector.connect(**DB_CONFIG)
            return conn
        except mysql.connector.Error as err:
            sys.stderr.write(f"Connection failed, retrying in 5s... {err}\n")
            retries -= 1
            time.sleep(5)
    return None

def bootstrap_db():
    """Bootstraps the mailboxData table if it does not exist[cite: 1]."""
    retries = 5
    while retries > 0:
        try:
            conn = get_db_connection()
            if not conn:
                raise Exception("Could not connect to DB")
            cursor = conn.cursor()
            cursor.execute("""
                CREATE TABLE IF NOT EXISTS mailboxData (
                    id INT AUTO_INCREMENT PRIMARY KEY,
                    datetime TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                    deviceID VARCHAR(50),
                    triggerEvent VARCHAR(50),
                    temp DECIMAL(5,2),
                    rssi INT
                )
            """)
            conn.commit()
            cursor.close()
            conn.close()
            print("Mailbox database bootstrapped successfully.")
            return
        except Exception as e:
            print(f"Database not ready, retrying... ({retries} left): {e}")
            retries -= 1
            time.sleep(5)

# --- Routes ---

@app.route('/api/mailboxData', methods=['GET'])
def get_mailbox_data():
    try:
        hours = request.args.get('hours', default=24, type=int)
        conn = get_db_connection()
        cursor = conn.cursor(dictionary=True)
        
        query = """
            SELECT id, datetime, deviceID, triggerEvent, temp, rssi 
            FROM mailboxData 
            WHERE datetime > NOW() - INTERVAL %s HOUR 
            ORDER BY datetime DESC
        """
        cursor.execute(query, (hours,))
        rows = cursor.fetchall()
        cursor.close()
        conn.close()

        return app.response_class(
            response=json.dumps(rows, default=datetime_handler),
            status=200,
            mimetype='application/json'
        )
    except Exception as e:
        print(f"ERROR: {str(e)}", file=sys.stderr)
        return jsonify([]), 200


@app.route('/api/latest-trigger', methods=['GET'])
def get_latest_trigger():
    try:
        conn = get_db_connection()
        cursor = conn.cursor(dictionary=True)
        
        # Using raw integers to match the INT type of triggerEvent
        query = """
            SELECT datetime, triggerEvent 
            FROM mailboxData 
            WHERE triggerEvent IN ('2', '3') 
            ORDER BY datetime DESC 
            LIMIT 1;
        """
        cursor.execute(query)
        row = cursor.fetchone() 
        cursor.close()
        conn.close()
    
        if row:
            evt_int = row['triggerEvent']
            mapping = {'2': 'Delivery', '3': 'Emptied'}
            latestTrigger = mapping.get(evt_int, 'Unknown')
            latestTriggerDate = row['datetime'].strftime('%-I:%M%p %B %d').replace('AM', 'am').replace('PM', 'pm') if hasattr(row['datetime'], 'strftime') else str(row['datetime'])
        else:
            latestTrigger = 'NONE'
            latestTriggerDate = datetime.now().strftime('%-I:%M%p %B %d').replace('AM', 'am').replace('PM', 'pm')

        return jsonify({"latestTriggerDate": latestTriggerDate, "latestTrigger": latestTrigger}), 200

    except Exception as e:
        print(f"ERROR: {str(e)}", file=sys.stderr)
        error_time = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
        return jsonify({"latestTriggerDate": error_time, "latestTrigger": "UNKNOWN"}), 200

@app.route('/api/time', methods=['GET'])
def get_time():
    ontario_tz = pytz.timezone('America/Toronto')
    now_ontario = datetime.now(ontario_tz)
    return jsonify({"time": now_ontario.strftime("%I:%M %p")})

@app.route('/', defaults={'path': ''})
@app.route('/<path:path>')
def serve(path):
    if path != "" and os.path.exists(os.path.join(app.static_folder, path)):
        return send_from_directory(app.static_folder, path)
    return send_from_directory(app.static_folder, 'index.html')

@app.route('/api/cl1p', methods=['POST'])
def handle_sync():
    urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)
    headers = {"cl1papitoken": CL1P_TOKEN} if CL1P_TOKEN else {}

    try:
        conn = get_db_connection()
        cursor = conn.cursor(dictionary=True)

        if LOCATION == 'home':
            cursor.execute("SELECT * FROM mailboxData WHERE datetime >= NOW() - INTERVAL 7 DAY")
            rows = cursor.fetchall()
            payload = json.dumps(rows, default=datetime_handler)
            
            requests.post(CL1P_URL, data=payload, headers=headers, verify=False, timeout=10)
            cursor.close()
            conn.close()
            return jsonify({"status": "pushed", "count": len(rows)})

        elif LOCATION == 'work':
            response = requests.get(CL1P_URL, headers=headers, verify=False, timeout=10)
            if response.status_code == 200 and response.text.strip():
                remote_data = json.loads(response.text)
                added_count = 0
                for item in remote_data:
                    cursor.execute("SELECT id FROM mailboxData WHERE datetime = %s", (item['datetime'],))
                    if not cursor.fetchone():
                        cursor.execute("""
                            INSERT INTO mailboxData (datetime, deviceID, triggerEvent, temp, rssi) 
                            VALUES (%s, %s, %s, %s, %s)
                        """, (item['datetime'], item['deviceID'], item['triggerEvent'], item['temp'], item['rssi']))
                        added_count += 1
                conn.commit()
                cursor.close()
                conn.close()
                return jsonify({"status": "pulled", "added": added_count})
            
            return jsonify({"status": "no data"}), 200

    except Exception as e:
        return jsonify({"error": str(e)}), 500

if __name__ == '__main__':
    bootstrap_db()
    port_env = int(os.getenv('API_PORT', 5002))
    app.run(host='0.0.0.0', port=port_env)