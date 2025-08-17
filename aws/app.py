from fastapi import FastAPI, HTTPException
from pydantic import BaseModel
from typing import List
import numpy as np
import tensorflow as tf

# ------------------- Load Models -------------------

# Load Keras models
clf_model = tf.keras.models.load_model("co_lstm_classifier_v1.h5")
reg_model = tf.keras.models.load_model("co_lstm_regression_v1.h5")

# ------------------- FastAPI -------------------
app = FastAPI()

# ------------------- Models -------------------

class SensorEvent(BaseModel):
    temperature: float
    humidity: float
    flowRate: float
    heaterVoltage: float
    sensorResistances: List[float]  # 14 items
    COppm: float  # Only used for classification

# ------------------- Endpoints -------------------

@app.post("/classify")
def classify(events: List[SensorEvent]):
    if len(events) != 10:
        raise HTTPException(status_code=400, detail="Classification model requires exactly 10 samples")

    input_sequence = [
        [e.temperature, e.humidity, e.heaterVoltage, e.COppm]
        for e in events
    ]

    input_array = np.array(input_sequence, dtype=np.float32).reshape(1, 10, 4)

    # Run inference
    predictions = clf_model.predict(input_array)
    class_index = int(np.argmax(predictions[0]))
    labels = ["safe", "warning", "danger"]

    return {
        "label": labels[class_index],
        "confidence": predictions[0].tolist()
    }

@app.post("/predict_ppm")
def predict_ppm(events: List[SensorEvent]):
    if len(events) != 30:
        raise HTTPException(status_code=400, detail="Regression model requires exactly 30 samples")

    input_sequence = [
        [
            e.temperature,
            e.humidity,
            e.heaterVoltage,
            e.flowRate,
            *e.sensorResistances  # Unpack the 14 values
        ]
        for e in events
    ]

    input_array = np.array(input_sequence, dtype=np.float32).reshape(1, 30, 18)

    # Run inference
    prediction = reg_model.predict(input_array)
    predicted_COppm = float(prediction[0][0])

    return {
        "predicted_COppm": predicted_COppm
    }