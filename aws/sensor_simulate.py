import csv
import time
import requests
import threading

# Replace with server endpoint
API_URL = "http://ec2-AA-BBB-AAA-X.us-west-1.compute.amazonaws.com:8000"

CLASSIFY_BUFFER_SIZE = 10
PREDICT_BUFFER_SIZE = 30
SEND_INTERVAL_SECONDS = 10  # Send data every 10 seconds

classify_buffer = []
predict_buffer = []

def row_to_sensor_event(row):
    return {
        "temperature": float(row["Temperature (C)"]),
        "humidity": float(row["Humidity (%r.h.)"]),
        "flowRate": float(row["Flow rate (mL/min)"]),
        "heaterVoltage": float(row["Heater voltage (V)"]),
        "sensorResistances": [float(row[f"R{i} (MOhm)"]) for i in range(1, 15)],
        "COppm": float(row["CO (ppm)"]),
    }

def test_classify(samples):
    start_time = time.time()
    response = requests.post(f"{API_URL}/classify", json=samples)
    end_time = time.time()
    print("\n--- Classification Response ---")
    print("Status Code:", response.status_code)
    print("Latency (seconds):", round(end_time - start_time, 4))
    print("Raw Response:", response.text)

def test_predict_ppm(samples):
    start_time = time.time()
    response = requests.post(f"{API_URL}/predict_ppm", json=samples)
    end_time = time.time()
    print("\n--- Regression Response ---")
    print("Status Code:", response.status_code)
    print("Latency (seconds):", round(end_time - start_time, 4))
    print("Raw Response:", response.text)

def process_row(row):
    global classify_buffer, predict_buffer
    event = row_to_sensor_event(row)

    classify_buffer.append(event)
    predict_buffer.append(event)

    if len(classify_buffer) == CLASSIFY_BUFFER_SIZE:
        samples = classify_buffer.copy()
        classify_buffer.clear()
        threading.Thread(target=test_classify, args=(samples,)).start()

    if len(predict_buffer) == PREDICT_BUFFER_SIZE:
        samples = predict_buffer.copy()
        predict_buffer.clear()
        threading.Thread(target=test_predict_ppm, args=(samples,)).start()

def main():
    with open("20161001_231809.csv", "r") as file:
        reader = csv.DictReader(file)
        for row in reader:
            process_row(row)
            time.sleep(SEND_INTERVAL_SECONDS)

if __name__ == "__main__":
    main()