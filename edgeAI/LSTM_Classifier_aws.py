import os
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt

from sklearn.preprocessing import MinMaxScaler, LabelEncoder
from sklearn.model_selection import train_test_split
from sklearn.metrics import classification_report, confusion_matrix, ConfusionMatrixDisplay
import tensorflow as tf
import coremltools as ct
from tensorflow.keras.models import Sequential
from tensorflow.keras.layers import LSTM, Dense
from tensorflow.keras.utils import to_categorical

# Set save directory
output_dir = "/Users/admin/Downloads"
os.makedirs(output_dir, exist_ok=True)

# --- Load and preprocess data ---
df = pd.read_csv("20160930_203718.csv")
df.columns = df.columns.str.strip()

# Simulate drift and noise on CO (optional, keep if you want)
df["CO_ppm_drifted"] = df["CO (ppm)"] + np.linspace(0, 0.001 * len(df), len(df))
df["CO_ppm_noisy"] = df["CO_ppm_drifted"] + np.random.normal(0, 0.05, size=df.shape[0])

# Create hazard labels from noisy CO
df["hazard_label"] = pd.cut(df["CO_ppm_noisy"], bins=[-np.inf, 50, 100, np.inf], labels=["safe", "warning", "danger"])
df = df.dropna(subset=["hazard_label"])

# --- UPDATED: include CO (ppm) as input feature ---
features = ["Temperature (C)", "Humidity (%r.h.)", "Heater voltage (V)", "CO (ppm)"]

# Normalize features
scaler = MinMaxScaler()
scaled_features = scaler.fit_transform(df[features])

# Encode labels
label_encoder = LabelEncoder()
labels = label_encoder.fit_transform(df["hazard_label"])
categorical_labels = to_categorical(labels)

# Create sequences
def create_sequences(X, y, time_steps=10):
    Xs, ys = [], []
    for i in range(len(X) - time_steps):
        Xs.append(X[i:i + time_steps])
        ys.append(y[i + time_steps])
    return np.array(Xs), np.array(ys)

X_seq, y_seq = create_sequences(scaled_features, categorical_labels, time_steps=10)

# Train/test split
X_train, X_test, y_train, y_test = train_test_split(X_seq, y_seq, test_size=0.2, random_state=42)

# --- Build LSTM model ---
model = Sequential()
model.add(LSTM(64, input_shape=(X_train.shape[1], X_train.shape[2]), return_sequences=False))
model.add(Dense(3, activation='softmax'))
model.compile(optimizer='adam', loss='categorical_crossentropy', metrics=['accuracy'])

# Train
history = model.fit(X_train, y_train, epochs=10, batch_size=32, validation_split=0.1)

# --- Evaluation ---
y_pred = model.predict(X_test)
y_pred_labels = np.argmax(y_pred, axis=1)
y_true_labels = np.argmax(y_test, axis=1)

# Save classification report
report_text = classification_report(y_true_labels, y_pred_labels, target_names=label_encoder.classes_)
with open(f"{output_dir}/classification_report.txt", "w") as f:
    f.write(report_text)
print("Saved classification_report.txt")

# Save confusion matrix PNG and TXT
cm = confusion_matrix(y_true_labels, y_pred_labels)
disp = ConfusionMatrixDisplay(confusion_matrix=cm, display_labels=label_encoder.classes_)
disp.plot(cmap=plt.cm.Blues)
plt.title("Confusion Matrix")
plt.savefig(f"{output_dir}/confusion_matrix.png", dpi=300, bbox_inches='tight')
plt.close()

pd.DataFrame(cm, index=label_encoder.classes_, columns=label_encoder.classes_).to_csv(
    f"{output_dir}/confusion_matrix.txt", sep='\t')
print("Saved confusion_matrix.png and .txt")

# Save classification metrics PNG and TXT
report_dict = classification_report(y_true_labels, y_pred_labels, target_names=label_encoder.classes_, output_dict=True)
report_df = pd.DataFrame(report_dict).transpose().iloc[:3]
report_df[['precision', 'recall', 'f1-score']].plot(kind='bar')
plt.title('Classification Metrics by Class')
plt.ylabel('Score')
plt.ylim(0, 1)
plt.xticks(rotation=0)
plt.grid(True)
plt.tight_layout()
plt.savefig(f"{output_dir}/classification_metrics.png", dpi=300)
plt.close()

report_df[['precision', 'recall', 'f1-score']].to_csv(f"{output_dir}/classification_metrics.txt", sep='\t', float_format='%.3f')
print("Saved classification_metrics.png and .txt")

# Save accuracy/loss curves PNG and TXT
plt.figure(figsize=(12, 5))
plt.subplot(1, 2, 1)
plt.plot(history.history['accuracy'], label='Train Accuracy')
plt.plot(history.history['val_accuracy'], label='Val Accuracy')
plt.title('Training vs Validation Accuracy')
plt.xlabel('Epoch')
plt.ylabel('Accuracy')
plt.legend()
plt.grid(True)

plt.subplot(1, 2, 2)
plt.plot(history.history['loss'], label='Train Loss')
plt.plot(history.history['val_loss'], label='Val Loss')
plt.title('Training vs Validation Loss')
plt.xlabel('Epoch')
plt.ylabel('Loss')
plt.legend()
plt.grid(True)

plt.tight_layout()
plt.savefig(f"{output_dir}/accuracy_loss_curves.png", dpi=300)
plt.close()

pd.DataFrame(history.history).to_csv(f"{output_dir}/accuracy_loss_history.txt", sep='\t', float_format='%.5f')
print("Saved accuracy_loss_curves.png and accuracy_loss_history.txt")

# Save Predicted vs True time series PNG and TXT
plt.figure(figsize=(15, 4))
plt.plot(y_true_labels[:200], label='True Labels', marker='o', alpha=0.6)
plt.plot(y_pred_labels[:200], label='Predicted Labels', marker='x', alpha=0.6)
plt.title('Predicted vs True Hazard Labels Over Time')
plt.xlabel('Sample Index')
plt.ylabel('Class Label')
plt.yticks([0, 1, 2], label_encoder.classes_)
plt.legend()
plt.grid(True)
plt.tight_layout()
plt.savefig(f"{output_dir}/predicted_vs_true_timeseries.png", dpi=300)
plt.close()

pd.DataFrame({
    'True Label': [label_encoder.classes_[i] for i in y_true_labels[:200]],
    'Predicted Label': [label_encoder.classes_[i] for i in y_pred_labels[:200]]
}).to_csv(f"{output_dir}/predicted_vs_true_labels.txt", sep='\t', index=False)
print("Saved predicted_vs_true_timeseries.png and predicted_vs_true_labels.txt")

# Export the model as a regular TensorFlow model (.h5) for server use (Amazon Linux / FastAPI) ---
model.save("co_lstm_classifier_v1.h5")
print("✅ Saved model as TensorFlow .h5: co_lstm_classifier_v1.h5")
