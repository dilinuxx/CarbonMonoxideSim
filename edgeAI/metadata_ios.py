from coremltools.models.utils import load_spec, save_spec

def add_coreml_metadata(input_path, output_path, author, license_str, description, version):
    spec = load_spec(input_path)
    spec.description.metadata.author = author
    spec.description.metadata.license = license_str
    spec.description.metadata.shortDescription = description
    spec.description.metadata.versionString = version
    save_spec(spec, output_path)
    print(f"Metadata added and saved to: {output_path}")

# Metadata values
author = "Emem Udoh"
license_str = "MIT"
version = "1.0.0"

# Regression model metadata
add_coreml_metadata(
    "co_lstm_regression_v1.mlmodel",
    "co_lstm_regression_v1_with_metadata.mlmodel",
    author,
    license_str,
    "LSTM regression model for predicting CO ppm from sensor data",
    version
)

# Classifier model metadata
add_coreml_metadata(
    "co_lstm_classifier_v1.mlmodel",
    "co_lstm_classifier_v1_with_metadata.mlmodel",
    author,
    license_str,
    "LSTM classification model for categorizing CO levels from sensor data",
    version
)
