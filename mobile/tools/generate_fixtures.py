"""Generate shared protocol fixtures from standard cryptographic primitives."""
import base64
import hashlib
import hmac
import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
password = "drawing-🔑-مرحبا"
salt, nonce = bytes(range(16)), bytes(range(32))
key = hashlib.pbkdf2_hmac("sha256", password.encode(), salt, 210000, 32)
b64 = lambda data: base64.b64encode(data).decode()
fixture = {
    "passkey": {"password": password, "salt": b64(salt), "nonce": b64(nonce),
                "proof": b64(hmac.new(key, nonce, hashlib.sha256).digest())},
    "layers": [
        {"id": "base", "name": "Background", "order": 0, "visible": True, "locked": False},
        {"id": "top", "name": "Details", "order": 1, "visible": True, "locked": False}],
    "objects": [
        {"id": "stroke-a", "layerId": "base", "type": "freehand", "color": "#202124",
         "fillColor": "#ffffff", "strokeWidth": 4, "fontSize": 24, "filled": False,
         "x": .1, "y": .2, "width": .7, "height": .5,
         "points": [{"x": .1, "y": .2}, {"x": .8, "y": .7}]},
        {"id": "rect-b", "layerId": "top", "type": "rectangle", "color": "#4488ff",
         "fillColor": "#ffffff", "strokeWidth": 8, "fontSize": 24, "filled": True,
         "x": .2, "y": .3, "width": .4, "height": .3, "points": []}]
}
path = root / "fixtures" / "protocol.json"
path.parent.mkdir(parents=True, exist_ok=True)
path.write_text(json.dumps(fixture, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
print(path)
