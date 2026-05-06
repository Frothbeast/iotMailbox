from flask import Flask, request, jsonify
import mysql.connector
import json
import os
import requests
from decimal import Decimal
from flask_cors import CORS
from dotenv import load_dotenv

load_dotenv()

app = Flask(__name__)
CORS(app)

DB_CONFIG = {
    'host': os.getenv('DB_HOST'),
    'user': os.getenv('DB_USER'),
    'password': os.getenv('DB_PASS'),
    'database': os.getenv('DB_NAME')
}

CL1P_URL = os.getenv('CL1P_URL_MAILBOX')
LOCATION = os.getenv('LOCATION')

def custom_serializer(obj):
    if isinstance(obj, Decimal):
        return float(obj)
    return str(obj)

@app.route('/api/mailboxData', methods=['GET'])
def get_mailbox_data():
    hours = request.args.get('hours', default=24, type=int)
    conn = None
    try:
        conn = mysql.connector.connect(**DB_CONFIG)
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
        return json.dumps(rows, default=custom_serializer), 200, {'Content-Type': 'application/json'}
    except Exception as e:
        return jsonify({"error": str(e)}), 500
    finally:
        if conn and conn.is_connected():
            conn.close()

@app.route('/api/sync', methods=['GET', 'POST'])
def sync_mailbox():
    conn = None
    try:
        conn = mysql.connector.connect(**DB_CONFIG)
        cursor = conn.cursor(dictionary=True)

        if LOCATION == 'home':
            cursor.execute("SELECT * FROM mailboxData WHERE datetime > NOW() - INTERVAL 7 DAY")
            data = cursor.fetchall()
            payload = json.dumps(data, default=custom_serializer)
            requests.post(CL1P_URL, data=payload, verify=False)
            cursor.close()
            return jsonify({"status": "pushed", "count": len(data)})

        elif LOCATION == 'work':
            response = requests.get(CL1P_URL, verify=False)
            if response.status_code == 200:
                remote_data = json.loads(response.text)
                added_count = 0
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
                return jsonify({"status": "pulled", "added": added_count})
            
            return jsonify({"status": "no_data_on_cl1p"}), 200

    except Exception as e:
        return jsonify({"error": str(e)}), 500
    finally:
        if conn and conn.is_connected():
            conn.close()

if __name__ == '__main__':
    port = int(os.getenv('API_PORT', 5002))
    app.run(host='0.0.0.0', port=port)