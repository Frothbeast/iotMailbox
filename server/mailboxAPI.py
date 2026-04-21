from flask import Flask, request, jsonify
import mysql.connector
import json
import os
import requests
import sys
from decimal import Decimal
from flask_cors import CORS
from dotenv import load_dotenv

load_dotenv()

app = Flask(__name__)
CORS(app)

# Environment Configuration
DB_CONFIG = {
    'host': os.getenv('DB_HOST'),
    'user': os.getenv('DB_USER'),
    'password': os.getenv('DB_PASS'),
    'database': os.getenv('DB_NAME')
}

CL1P_URL = os.getenv('CL1P_URL_MAILBOX')
LOCATION = os.getenv('LOCATION')  # 'home' or 'work'

def get_db_connection():
    return mysql.connector.connect(**DB_CONFIG)

def custom_serializer(obj):
    """JSON serializer for objects not serializable by default json code"""
    if isinstance(obj, Decimal):
        return float(obj)
    return str(obj)

@app.route('/api/mailboxData', methods=['GET'])
def get_mailbox_data():
    """Fetch recent mailbox events for the frontend."""
    hours = request.args.get('hours', default=24, type=int)
    try:
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
        return json.dumps(rows, default=custom_serializer), 200, {'Content-Type': 'application/json'}
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route('/api/sync', methods=['GET', 'POST'])
def sync_mailbox():
    """
    Handles synchronization via Cl1p.
    Home: Pushes local DB records to Cl1p.
    Work: Pulls records from Cl1p and inserts missing ones into local DB.
    """
    try:
        conn = get_db_connection()
        cursor = conn.cursor(dictionary=True)

        if LOCATION == 'home':
            # 1. Fetch last 7 days of data to push
            cursor.execute("SELECT * FROM mailboxData WHERE datetime > NOW() - INTERVAL 7 DAY")
            data = cursor.fetchall()
            
            # 2. Push to Cl1p
            payload = json.dumps(data, default=custom_serializer)
            requests.post(CL1P_URL, data=payload, verify=False)
            
            cursor.close()
            conn.close()
            return jsonify({"status": "pushed", "count": len(data)})

        elif LOCATION == 'work':
            # 1. Pull from Cl1p
            response = requests.get(CL1P_URL, verify=False)
            if response.status_code == 200:
                remote_data = json.loads(response.text)
                added_count = 0
                
                # 2. Insert only if timestamp doesn't exist
                for item in remote_data:
                    cursor.execute("SELECT id FROM mailboxData WHERE datetime = %s", (item['datetime'],))
                    if not cursor.fetchone():
                        insert_query = """
                            INSERT INTO mailboxData (datetime, deviceID, triggerEvent, temp, rssi) 
                            VALUES (%s, %s, %s, %s, %s)
                        """
                        cursor.execute(insert_query, (
                            item['datetime'], item['deviceID'], 
                            item['triggerEvent'], item['temp'], item['rssi']
                        ))
                        added_count += 1
                
                conn.commit()
                cursor.close()
                conn.close()
                return jsonify({"status": "pulled", "added": added_count})
            
            return jsonify({"status": "no_data_on_cl1p"}), 200

    except Exception as e:
        return jsonify({"error": str(e)}), 500

if __name__ == '__main__':
    # Defaulting to 5002 to avoid collision with standard heater ports if running on same host
    port = int(os.getenv('MAILBOX_API_PORT', 5002))
    app.run(host='0.0.0.0', port=port)