FROM python:3.13-slim
RUN apt-get update && apt-get install -y --no-install-recommends ffmpeg && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt
COPY bridge.py link_google.py ./
ENV SONIX_YTM_STATE=/state
CMD ["gunicorn", "--bind", "0.0.0.0:18765", "--workers", "1", "--threads", "6", "bridge:app"]
