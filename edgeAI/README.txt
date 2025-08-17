LSTM_Model – Time Series Forecasting with LSTM
=============================================

This project provides a Long Short-Term Memory (LSTM) model for time series 
forecasting and includes support for Core ML conversion. Please follow the 
instructions below to set up your environment and run the model script.

----------------------------------------------------------------------
1. Environment Setup
----------------------------------------------------------------------

Step 1: Create and activate a Python virtual environment
Windows:
    > python -m venv tfcoreml-env
    > tfcoreml-env\Scripts\activate

macOS/Linux:
    $ python3 -m venv tfcoreml-env
    $ source tfcoreml-env/bin/activate

You should see the prompt change to show (tfcoreml-env), indicating that
the environment is active.

----------------------------------------------------------------------
2. Install Required Dependencies
----------------------------------------------------------------------

Install the following libraries with the specified versions for compatibility:
    pip install coremltools==6.3.0
    pip install scikit-learn==1.1.2
    pip install numpy==1.24.3
    pip install matplotlib
    pip install tensorflow
    pip install pandas

    OR

    pip install coremltools==6.3.0 scikit-learn==1.1.2 numpy==1.24.3 matplotlib tensorflow pandas



IMPORTANT:
----------
* DO NOT use scikit-learn version 1.2.2
* Supported version range: >= 0.17, <= 1.1.2
* Using unsupported versions may disable Core ML conversion APIs in coremltools.

----------------------------------------------------------------------
3. Run the LSTM Model
----------------------------------------------------------------------

Once all dependencies are installed and the virtual environment is active,
run the following command:

    python LSTM_Model.py
    python LSTM_Classifier.py

This will execute the training and conversion logic as defined in the script.

----------------------------------------------------------------------
4. Troubleshooting
----------------------------------------------------------------------

* Ensure you're in the virtual environment (you should see (tfcoreml-env) in your terminal)
* Double-check that all packages match the required versions
* Reinstall packages using --force-reinstall if necessary
* If conversion fails, confirm scikit-learn is at or below version 1.1.2

----------------------------------------------------------------------