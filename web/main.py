# Serve the Esp-WiFi-BLE console.  Run:  uvicorn main:app --host localhost --port 8000
# Then open http://localhost:8000  (a secure context, so Web Bluetooth works).
from pathlib import Path
from fastapi import FastAPI
from fastapi.responses import FileResponse

app = FastAPI()
INDEX = Path(__file__).parent / "index.html"


@app.get("/")
def index():
    return FileResponse(INDEX)

if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host="localhost", port=8000)