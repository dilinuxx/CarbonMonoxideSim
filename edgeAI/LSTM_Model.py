import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from sklearn.preprocessing import MinMaxScaler, LabelEncoder
from sklearn.metrics import mean_squared_error, mean_absolute_error, r2_score, classification_report
from math import sqrt
import tensorflow as tf
import coremltools as ct
from tensorflow.keras.models import Sequential
from tensorflow.keras.layers import LSTM, Dense, Dropout, Input
from tensorflow.keras.optimizers import Adam
from tensorflow.keras.callbacks import EarlyStopping
from sklearn.model_selection import train_test_split
from tensorflow.keras.utils import to_categorical


# --- Step 1: Load and preprocess data ---
csv_files = ["20160930_203718.csv"]
df_list = [pd.read_csv(file) for file in csv_files]
df = pd.concat(df_list, ignore_index=True)
df.columns = df.columns.str.strip()
print("Cleaned column names:", df.columns.tolist())

# --- Step 2: Simulate drift and noise (optional) ---
def simulate_drift(data, drift_rate=0.001):
    drift = np.linspace(0, drift_rate * len(data), len(data))
    return data + drift

def add_noise(data, std_dev=0.05):
    return data + np.random.normal(0, std_dev, size=data.shape)

df["CO_ppm_drifted"] = simulate_drift(df["CO (ppm)"])
df["CO_ppm_noisy"] = add_noise(df["CO_ppm_drifted"])

# --- Step 3: Select and scale features and target ---
features = [
    "Temperature (C)",
    "Humidity (%r.h.)",
    "Heater voltage (V)",
    "Flow rate (mL/min)"
] + [f"R{i} (MOhm)" for i in range(1, 15)]  # Sensor resistances

target = "CO_ppm_noisy"

scaler_X = MinMaxScaler()
X_scaled = scaler_X.fit_transform(df[features])

scaler_y = MinMaxScaler()
y_scaled = scaler_y.fit_transform(df[target].values.reshape(-1, 1))

# --- Step 4: Prepare data for LSTM ---
def create_sequences(X, y, time_steps=30):
    Xs, ys = [], []
    for i in range(len(X) - time_steps):
        Xs.append(X[i:i + time_steps])
        ys.append(y[i + time_steps])
    return np.array(Xs), np.array(ys)

time_steps = 30
X_seq, y_seq = create_sequences(X_scaled, y_scaled, time_steps)

# --- Step 5: Train-test split (chronological, no shuffle) ---
split_index = int(len(X_seq) * 0.8)
X_train, X_test = X_seq[:split_index], X_seq[split_index:]
y_train, y_test = y_seq[:split_index], y_seq[split_index:]

# --- Step 6: Further split train into train/val for metrics callback ---
val_split = 0.1
val_samples = int(len(X_train) * val_split)
X_val = X_train[-val_samples:]
y_val = y_train[-val_samples:]
X_train_final = X_train[:-val_samples]
y_train_final = y_train[:-val_samples]

# --- Step 7: Build LSTM model ---
model = Sequential([
    Input(shape=(X_train_final.shape[1], X_train_final.shape[2])),
    LSTM(128, return_sequences=True),
    Dropout(0.2),
    LSTM(64),
    Dropout(0.2),
    Dense(1)
])

optimizer = Adam(learning_rate=0.001)
model.compile(optimizer=optimizer, loss='mse')

# --- Step 8: Custom callback to print and store RMSE, MAE, R2 per epoch ---
class MetricsCallback(tf.keras.callbacks.Callback):
    def __init__(self, train_data, val_data):
        super().__init__()
        self.X_train, self.y_train = train_data
        self.X_val, self.y_val = val_data
        self.train_rmse = []
        self.val_rmse = []
        self.train_mae = []
        self.val_mae = []
        self.train_r2 = []
        self.val_r2 = []

    def on_epoch_end(self, epoch, logs=None):
        y_train_pred = self.model.predict(self.X_train, verbose=0)
        y_val_pred = self.model.predict(self.X_val, verbose=0)

        y_train_true_unscaled = scaler_y.inverse_transform(self.y_train)
        y_train_pred_unscaled = scaler_y.inverse_transform(y_train_pred)
        y_val_true_unscaled = scaler_y.inverse_transform(self.y_val)
        y_val_pred_unscaled = scaler_y.inverse_transform(y_val_pred)

        def calc_metrics(true, pred):
            rmse = sqrt(mean_squared_error(true, pred))
            mae = mean_absolute_error(true, pred)
            r2 = r2_score(true, pred)
            return rmse, mae, r2

        train_rmse, train_mae, train_r2 = calc_metrics(y_train_true_unscaled, y_train_pred_unscaled)
        val_rmse, val_mae, val_r2 = calc_metrics(y_val_true_unscaled, y_val_pred_unscaled)

        self.train_rmse.append(train_rmse)
        self.val_rmse.append(val_rmse)
        self.train_mae.append(train_mae)
        self.val_mae.append(val_mae)
        self.train_r2.append(train_r2)
        self.val_r2.append(val_r2)

        print(f"Epoch {epoch + 1}:")
        print(f"  Train - RMSE: {train_rmse:.4f}, MAE: {train_mae:.4f}, R2: {train_r2:.4f}")
        print(f"  Val   - RMSE: {val_rmse:.4f}, MAE: {val_mae:.4f}, R2: {val_r2:.4f}")

# --- Step 9: Train model ---
early_stop = EarlyStopping(monitor='val_loss', patience=10, restore_best_weights=True)
metrics_callback = MetricsCallback((X_train_final, y_train_final), (X_val, y_val))

history = model.fit(
    X_train_final, y_train_final,
    epochs=100,
    batch_size=32,
    validation_data=(X_val, y_val),
    callbacks=[early_stop, metrics_callback],
    verbose=2
)

# --- Step 10: Evaluate on test set ---
y_pred = model.predict(X_test)
y_pred_unscaled = scaler_y.inverse_transform(y_pred)
y_test_unscaled = scaler_y.inverse_transform(y_test)

rmse = sqrt(mean_squared_error(y_test_unscaled, y_pred_unscaled))
mae = mean_absolute_error(y_test_unscaled, y_pred_unscaled)
r2 = r2_score(y_test_unscaled, y_pred_unscaled)

print(f"Test Set Metrics: RMSE={rmse:.4f}, MAE={mae:.4f}, R2={r2:.4f}")

# --- Step 11: Plot metrics and predictions ---

epochs = range(1, len(metrics_callback.train_rmse) + 1)

# 1. Training and Validation RMSE over epochs
plt.figure(figsize=(10,5))
plt.plot(epochs, metrics_callback.train_rmse, 'b-', label='Train RMSE')
plt.plot(epochs, metrics_callback.val_rmse, 'r-', label='Validation RMSE')
plt.xlabel('Epochs')
plt.ylabel('RMSE')
plt.title('Training and Validation RMSE over Epochs')
plt.legend()
plt.grid(True)
plt.show()

# 2. Training and Validation R² over epochs
plt.figure(figsize=(10,5))
plt.plot(epochs, metrics_callback.train_r2, 'b-', label='Train R²')
plt.plot(epochs, metrics_callback.val_r2, 'r-', label='Validation R²')
plt.xlabel('Epochs')
plt.ylabel('R² Score')
plt.title('Training and Validation R² over Epochs')
plt.legend()
plt.grid(True)
plt.show()

# 3. Predicted vs Actual CO ppm on test set
plt.figure(figsize=(8,8))
plt.scatter(y_test_unscaled, y_pred_unscaled, alpha=0.5, s=10)
plt.plot([y_test_unscaled.min(), y_test_unscaled.max()], [y_test_unscaled.min(), y_test_unscaled.max()], 'k--', lw=2)
plt.xlabel('Actual CO ppm')
plt.ylabel('Predicted CO ppm')
plt.title('Predicted vs Actual CO Concentration on Test Set')
plt.grid(True)
plt.show()

# 4. Residual plot (Prediction error distribution)
residuals = y_test_unscaled - y_pred_unscaled
plt.figure(figsize=(10,5))
plt.scatter(y_test_unscaled, residuals, alpha=0.5, s=10)
plt.axhline(y=0, color='r', linestyle='--')
plt.xlabel('Actual CO ppm')
plt.ylabel('Residual (Actual - Predicted)')
plt.title('Residual Plot on Test Set')
plt.grid(True)
plt.show()

# --- Step 12: Export the model to TensorFlow Lite (for Android) ---
converter = tf.lite.TFLiteConverter.from_keras_model(model)

# Allow TF Select Ops and disable lowering tensor list ops to fix 'tf.TensorListReserve' error
converter.target_spec.supported_ops = [
    tf.lite.OpsSet.TFLITE_BUILTINS,
    tf.lite.OpsSet.SELECT_TF_OPS
]
converter._experimental_lower_tensor_list_ops = False
tflite_model = converter.convert()
with open("co_lstm_regression_v1.tflite", "wb") as f:
    f.write(tflite_model)

print("✅ Exported to TFLite: co_lstm_regression_v1.tflite")

# --- Step 13: Export the model to CoreML (for iOS) ---
mlmodel = ct.convert(
    model,
    inputs=[ct.TensorType(shape=(1, X_train.shape[1], X_train.shape[2]))]
)
mlmodel.save("co_lstm_regression_v1.mlmodel")
print("✅ Exported to CoreML: co_lstm_regression_v1.mlmodel")