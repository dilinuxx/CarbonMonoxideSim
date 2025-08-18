import random
import time
import numpy as np
from tensorflow.keras.models import load_model

# ---------- Sensor Event Generator ----------
def generate_sensor_event(co_ppm=None):
    return {
        "temperature": round(random.uniform(20.0, 30.0), 2),
        "humidity": round(random.uniform(40.0, 60.0), 2),
        "flowRate": round(random.uniform(230.0, 270.0), 4),
        "heaterVoltage": round(random.uniform(0.85, 0.95), 4),
        "sensorResistances": [round(random.uniform(0.1, 0.15), 4) for _ in range(14)],
        "COppm": co_ppm if co_ppm is not None else round(random.uniform(0.0, 10.0), 2)
    }

# ---------- Preprocessors ----------
def preprocess_for_classification_sequence():
    sequence = []
    for _ in range(10):  # 10 timesteps
        sample = generate_sensor_event()
        features = [
            sample["temperature"],
            sample["humidity"],
            sample["heaterVoltage"],
            sample["COppm"]
        ]
        sequence.append(features)
    return sequence  # (10, 4)

def preprocess_for_regression_sequence():
    sequence = []
    for _ in range(30):  # 30 timesteps
        sample = generate_sensor_event(co_ppm=0.0)
        features = [
            sample["temperature"],
            sample["humidity"],
            sample["heaterVoltage"],
            sample["flowRate"]
        ] + sample["sensorResistances"]
        sequence.append(features)
    return sequence  # (30, 18)

# ---------- Benchmark Functions ----------
def test_classify(model):
    times = []
    print("\n--- Classification Benchmark ---")
    for i in range(10):
        X = np.array([preprocess_for_classification_sequence()])
        start_time = time.time()
        predictions = model.predict(X, verbose=0)
        end_time = time.time()
        duration_ms = (end_time - start_time) * 1000
        times.append(duration_ms)

        print(f"Run {i+1}: {duration_ms:.3f} ms → {predictions[0]}")

    avg_time = np.mean(times)
    print(f"\nAverage Inference Time (Classification): {avg_time:.3f} ms")

def test_predict_ppm(model):
    times = []
    print("\n--- Regression Benchmark ---")
    for i in range(10):
        X = np.array([preprocess_for_regression_sequence()])
        start_time = time.time()
        predictions = model.predict(X, verbose=0)
        end_time = time.time()
        duration_ms = (end_time - start_time) * 1000
        times.append(duration_ms)

        print(f"Run {i+1}: {duration_ms:.3f} ms → {predictions.flatten()[0]:.5f}")

    avg_time = np.mean(times)
    print(f"\nAverage Inference Time (Regression): {avg_time:.3f} ms")

# ---------- Main ----------
if __name__ == "__main__":
    print("Loading models...")
    classifier_model = load_model("co_lstm_classifier_v1.h5")
    regression_model = load_model("co_lstm_regression_v1.h5")

    test_classify(classifier_model)
    test_predict_ppm(regression_model)
