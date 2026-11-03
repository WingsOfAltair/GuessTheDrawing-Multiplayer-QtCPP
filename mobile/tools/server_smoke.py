"""Exercise the native wire contract against isolated real Qt services.

Usage: python mobile/tools/server_smoke.py --bin build/Debug --qt-bin C:/Qt/6.9.1/msvc2022_64/bin
Add --android to run the native Android instrumentation scenario on a running emulator.
No production config, accounts, ports or databases are modified.
"""
import argparse
import base64
import hashlib
import hmac
import json
import os
from pathlib import Path
import secrets
import socket
import sqlite3
import subprocess
import tempfile
import time
import uuid

MOBILE = Path(__file__).resolve().parents[1]
VERSION = (MOBILE / "GameVersion.xcconfig").read_text().split("=")[1].strip()
PASSWORD = "mobile-room-passkey"

def available_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]

class Peer:
    def __init__(self, port):
        self.socket = socket.create_connection(("127.0.0.1", port), timeout=5)
        self.pending = b""
        self.messages = []
    def send(self, kind, **fields):
        message = dict(fields, type=kind, version=1, appVersion=VERSION)
        self.socket.sendall((json.dumps(message, ensure_ascii=False) + "\n").encode())
    def wait(self, kind, timeout=15):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            for i, message in enumerate(self.messages):
                if message["type"] in ("VersionMismatch", "Error"):
                    raise AssertionError(message.get("message", message.get("requiredVersion")))
                if message["type"] == kind:
                    return self.messages.pop(i)
            self.socket.settimeout(max(.1, end - time.monotonic()))
            data = self.socket.recv(65536)
            if not data:
                raise AssertionError("Server disconnected")
            self.pending += data
            while b"\n" in self.pending:
                line, self.pending = self.pending.split(b"\n", 1)
                if line.strip():
                    self.messages.append(json.loads(line))
        raise AssertionError("Timed out waiting for " + kind)
    def close(self):
        self.socket.close()

def join(peer, token, spectator=False):
    request = uuid.uuid4().hex
    fields = dict(requestId=request, roomId="mobile-test", authToken=token, spectator=spectator)
    peer.send("JoinRoom", **fields)
    challenge = peer.wait("RoomPasskeyChallenge")
    key = hashlib.pbkdf2_hmac("sha256", PASSWORD.encode(),
        base64.b64decode(challenge["passkeySalt"]), 210000, 32)
    proof = hmac.new(key, base64.b64decode(challenge["passkeyChallenge"]), hashlib.sha256).digest()
    peer.send("JoinRoom", **fields, passkeyProof=base64.b64encode(proof).decode())
    result = peer.wait("JoinResult")
    assert result["ok"], result.get("error")
    return result

def main():
    global VERSION
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bin", type=Path, required=True)
    parser.add_argument("--qt-bin", type=Path)
    parser.add_argument("--android", action="store_true")
    parser.add_argument("--adb", type=Path)
    parser.add_argument("--version", default=VERSION, help="Exact version of the server binaries under test")
    args = parser.parse_args()
    VERSION = args.version
    env = os.environ.copy()
    for key in ("GTD_SESSION_SECRET", "GTD_SERVER_SECRET", "GTD_ROOM_PASSKEY"):
        env.pop(key, None)
    if args.qt_bin:
        env["PATH"] = str(args.qt_bin.resolve()) + os.pathsep + env.get("PATH", "")
        env["QT_PLUGIN_PATH"] = str(args.qt_bin.resolve().parent / "plugins")
    binary = args.bin.resolve()
    suffix = ".exe" if os.name == "nt" else ""
    master_port, game_port = available_port(), available_port()
    while game_port == master_port:
        game_port = available_port()
    processes, peers, logs = [], [], []
    adb = str(args.adb or Path(env.get("ANDROID_HOME", "")) / "platform-tools" / ("adb.exe" if os.name == "nt" else "adb"))
    with tempfile.TemporaryDirectory(prefix="gtd-mobile-") as folder:
        folder = Path(folder)
        config = {
            "version": 1,
            "security": {"serverSecret": secrets.token_hex(32), "sessionSecret": secrets.token_hex(32)},
            "master": {"address": "127.0.0.1", "port": master_port, "database": str(folder / "test.sqlite")},
            "gameServer": {
                "masterAddress": "127.0.0.1", "masterPort": master_port,
                "serverId": "mobile-test", "roomId": "mobile-test", "roomName": "Mobile test room",
                "roomPasskey": PASSWORD, "publicHost": "127.0.0.1", "port": game_port,
                "roundSeconds": 300, "intermissionSeconds": 0, "maxPlayers": 8,
                "profanityDictionaryPath": str(binary / "profanity-dictionary.txt")},
            "client": {"dictionaryPath": str(binary / "dictionary")}
        }
        config_path = folder / "config.json"
        config_path.write_text(json.dumps(config), encoding="utf-8")
        try:
            for name in ("gtd_master", "gtd_game_server"):
                log = (folder / (name + ".log")).open("w+", encoding="utf-8")
                logs.append(log)
                processes.append(subprocess.Popen([str(binary / (name + suffix)), "--config", str(config_path)],
                    cwd=binary, env=env, stdout=log, stderr=subprocess.STDOUT,
                    creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0))
                deadline = time.monotonic() + 12
                port = master_port if name == "gtd_master" else game_port
                while True:
                    try:
                        probe = Peer(port); probe.close(); break
                    except OSError:
                        if time.monotonic() > deadline or processes[-1].poll() is not None:
                            log.seek(0); raise AssertionError(log.read())
                        time.sleep(.1)
            if args.android:
                # A test-only staff account in the disposable database, never the user's server.
                admin = Peer(master_port)
                admin.send("CreateAccount", requestId="test-admin", username="mobile_test_admin",
                           password="MobileTest123!", displayName="Mobile moderator")
                assert admin.wait("AuthResult")["ok"]
                admin.close()
                with sqlite3.connect(folder / "test.sqlite") as db:
                    db.execute("INSERT INTO master_super_admins(username) VALUES (?)", ("mobile_test_admin",))
                db.close()
                for port in (master_port, game_port):
                    subprocess.run([adb, "-e", "reverse", f"tcp:{port}", f"tcp:{port}"], check=True)
                gradle = MOBILE / "android" / ("gradlew.bat" if os.name == "nt" else "gradlew")
                subprocess.run([str(gradle), "--no-daemon", "connectedDebugAndroidTest",
                    f"-PgtdVersion={VERSION}",
                    "-PgtdApplicationId=com.guessthedrawing.mobile.regression",
                    f"-Pandroid.testInstrumentationRunnerArguments.masterPort={master_port}"],
                    cwd=gradle.parent, env=env, check=True)
                print("PASS: Android native login, social, inbox, profiles, curves, voice, moderation and game integration")
                return
            tokens = []
            for i in range(3):
                p = Peer(master_port); peers.append(p)
                p.send("CreateAccount", requestId=str(i), username="mobile_" + uuid.uuid4().hex[:12],
                       password="MobileTest123!", displayName="Mobile " + str(i))
                auth = p.wait("AuthResult")
                assert auth["ok"], auth.get("error")
                tokens.append(auth["authToken"])
            deadline = time.monotonic() + 12
            while True:
                peers[0].send("ListRooms", authToken=tokens[0], requestId="rooms")
                rooms = peers[0].wait("RoomList")["rooms"]
                if rooms:
                    assert rooms[0]["privateRoom"]
                    break
                assert time.monotonic() < deadline, "Room registration timeout"
                time.sleep(.1)
            a, b, spectator = Peer(game_port), Peer(game_port), Peer(game_port)
            peers += [a, b, spectator]
            join(a, tokens[0]); join(b, tokens[1])
            started = a.wait("RoundStarted")
            # The first player becomes the first drawer.
            assigned = a.wait("WordAssigned")
            fixture = json.loads((MOBILE / "fixtures" / "protocol.json").read_text(encoding="utf-8"))
            stroke = fixture["objects"][0] | {"layerId": "layer-default"}
            a.send("CanvasOperation", operation={"action": "object_upsert", "object": stroke})
            event = b.wait("CanvasOperation")
            assert event["operation"]["object"]["id"] == stroke["id"]
            late = join(spectator, tokens[2], spectator=True)
            assert late["spectator"] and any(o["id"] == stroke["id"] for o in late["objects"])
            spectator.send("RoomChat", text="Hello from mobile", requestId="chat")
            assert b.wait("ChatMessage")["text"] == "Hello from mobile"
            b.send("Guess", text=assigned["word"])
            assert b.wait("GuessCorrect")["points"] > 0
            assert a.wait("RoundEnded")["word"] == assigned["word"]
            spectator.send("LeaveRoom")
            spectator.wait("LeaveAck")
            print("PASS: Qt auth, room list, private proof, round, drawing relay, late snapshot, chat, guess, leave")
        except Exception:
            for log in logs:
                log.flush()
                log.seek(0)
                print(Path(log.name).name + ":\n" + log.read()[-5000:])
            raise
        finally:
            for peer in peers:
                peer.close()
            if args.android:
                for port in (master_port, game_port):
                    subprocess.run([adb, "-e", "reverse", "--remove", f"tcp:{port}"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            for process in processes:
                process.terminate()
            for process in processes:
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill(); process.wait()
            for log in logs:
                log.close()

if __name__ == "__main__":
    main()
