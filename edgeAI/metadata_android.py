from tflite_support.metadata_writers import writer_utils
from tflite_support.metadata_writers import MetadataWriter
from tflite_support.metadata_writers.text_classifier import TextClassifierWriter  # Dummy, just to access generic writer
from tflite_support.metadata.schema_py_generated import ModelMetadataT

def add_tflite_metadata(model_path, output_path, description, author, version, license_str):
    # Load the model
    model_buf = open(model_path, "rb").read()

    # Create metadata writer
    writer = MetadataWriter.create_from_buffer(model_buf)
    model_metadata = writer.get_metadata()
    
    # Set metadata fields
    model_metadata.name = model_path
    model_metadata.description = description
    model_metadata.author = author
    model_metadata.version = version
    model_metadata.license = license_str

    # Serialize and write new model
    writer_utils.save_file(writer.populate(), output_path)
    print(f"Metadata added and saved to: {output_path}")

# Metadata values
author = "Emem Udoh"
license_str = "MIT"
version = "1.0.0"

# Regression model
add_tflite_metadata(
    "co_lstm_regression_v1.tflite",
    "co_lstm_regression_v1_with_metadata.tflite",
    "LSTM regression model for predicting CO ppm from sensor data",
    author,
    version,
    license_str
)

# Classifier model
add_tflite_metadata(
    "co_lstm_classifier_v1.tflite",
    "co_lstm_classifier_v1_with_metadata.tflite",
    "LSTM classification model for categorizing CO levels from sensor data",
    author,
    version,
    license_str
)
