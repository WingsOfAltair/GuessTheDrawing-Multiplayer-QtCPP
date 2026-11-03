"""Validate portable project metadata and protocol fixtures on any development host."""
from pathlib import Path
import json
import plistlib
import re
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parents[1]
version = (root / "GameVersion.xcconfig").read_text().split("=")[1].strip()
assert re.fullmatch(r"\d+\.\d+\.\d+", version), version
cmake = (root.parent / "CMakeLists.txt").read_text()
source_version = re.search(r"project\(GuessTheDrawing VERSION (\d+\.\d+\.\d+)", cmake)[1]
assert version == source_version, f"Mobile {version} and CMake {source_version} differ"
project = (root / "ios/GuessTheDrawing.xcodeproj/project.pbxproj").read_text()
for path in (root / "ios/GuessTheDrawing").rglob("*.swift"):
    assert path.relative_to(root / "ios").as_posix() in project, path
for path in root.rglob("*.plist"):
    with path.open("rb") as file:
        plistlib.load(file)
for path in (root / "ios/GuessTheDrawing/Assets.xcassets").rglob("*.json"):
    json.loads(path.read_text())
for path in (root / "android/app/src/main").rglob("*.xml"):
    ET.parse(path)
ET.parse(root / "ios/GuessTheDrawing.xcodeproj/xcshareddata/xcschemes/GuessTheDrawing.xcscheme")
fixture = json.loads((root / "fixtures/protocol.json").read_text(encoding="utf-8"))
assert len(fixture["objects"]) == 2
assert (root / "android/gradle/wrapper/gradle-wrapper.jar").is_file()
print("PASS: version synchronization, Xcode source references, plist/XML/assets, fixtures and Gradle wrapper")
