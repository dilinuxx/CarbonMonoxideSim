-----------------------
AWS Linux Requirements
-----------------------
fastapi==0.110.0
uvicorn[standard]==0.29.0

# TensorFlow Lite Runtime (lightweight alternative to full tensorflow)
tflite-runtime==2.13.0

# Numerical and scientific stack
numpy<2
scikit-learn==1.4.2
pandas==2.2.2
matplotlib==3.8.4

# Optional: if you want to enable CORS for mobile/web clients
# fastapi[all] would include this, or install separately:
# pip install "fastapi[all]"
python-multipart==0.0.9

----------------------
Environment
----------------------
python -m venv lstm_api_env
source lstm_api_env/bin/activate

pip install --upgrade pip
pip install -r README.txt

nohup uvicorn app:app --host 0.0.0.0 --port 8000 > server.log 2>&1 &


