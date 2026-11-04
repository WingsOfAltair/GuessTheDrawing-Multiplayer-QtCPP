"""Validate catalog completeness, native resource fidelity, placeholders and RTL wiring."""
from pathlib import Path
import hashlib
import json
import re
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parents[1]
catalog = json.loads((root/'localization/catalog.json').read_text(encoding='utf-8'))
codes = ['en','es','fr','de','pt','it','ru','zh','ja','ko','ar','hi']
assert list(catalog) == codes
keys = set(catalog['en'])
required = set(json.loads((root/'localization/required.json').read_text(encoding='utf-8')))
assert required <= keys
assert not (root/'localization/missing.txt').exists(), 'Regenerate the complete catalog'
def placeholders(value): return set(re.findall(r'%\d+', value))
def android_value(value):
    assert value.startswith('"') and value.endswith('"')
    return re.sub(r'\\(.)', lambda m: {'n':'\n','r':'\r','t':'\t'}.get(m[1],m[1]), value[1:-1])
project = (root/'ios/GuessTheDrawing.xcodeproj/project.pbxproj').read_text(encoding='utf-8')
for code in codes:
    assert set(catalog[code]) == keys, code
    for key, value in catalog[code].items():
        assert value and '\ufffd' not in value, (code,key)
        assert placeholders(value) <= placeholders(key), (code,key,value)
        # Every argument in mobile-owned messages must survive translation.
        if key in ['@%1 invited you to %2','By @%1 · %2','%1 unread · %2','Server requires version %1. Install a matching mobile build.']:
            assert placeholders(key) == placeholders(value), (code,key)
    directory = 'values' if code == 'en' else 'values-'+('zh-rCN' if code == 'zh' else code)
    xml = ET.parse(root/'android/app/src/main/res'/directory/'strings.xml').getroot()
    values = {element.attrib['name']: android_value(element.text) for element in xml}
    assert len(values) == len(keys), code
    for key in keys:
        identifier = 'l_'+hashlib.sha1(key.encode()).hexdigest()[:12]
        assert values[identifier] == catalog[code][key], (code,key,'Android')
    region = 'zh-Hans' if code == 'zh' else code
    path = root/'ios/GuessTheDrawing'/(region+'.lproj')/'Localizable.strings'
    swift = {}
    for line in path.read_text(encoding='utf-8').splitlines():
        if line.startswith('//') or not line: continue
        match = re.fullmatch(r'("(?:[^"\\]|\\.)*") = ("(?:[^"\\]|\\.)*");', line)
        assert match, (path,line)
        swift[json.loads(match[1])] = json.loads(match[2])
    assert swift == catalog[code], (code,'iOS')
    for name in ['Localizable.strings','InfoPlist.strings']:
        assert f'GuessTheDrawing/{region}.lproj/{name}' in project, (region,name)
    permission = (path.parent/'InfoPlist.strings').read_text(encoding='utf-8')
    assert 'NSMicrophoneUsageDescription' in permission and 'NSLocalNetworkUsageDescription' in permission

android = root/'android/app/src/main'
manifest = ET.parse(android/'AndroidManifest.xml').getroot()
assert manifest.find('application').get('{http://schemas.android.com/apk/res/android}supportsRtl') == 'true'
assert 'enableSplit = false' in (root/'android/app/build.gradle.kts').read_text()
assert 'View.LAYOUT_DIRECTION_LTR' in (android/'java/com/guessthedrawing/mobile/DrawingView.kt').read_text()
assert '.forceLeftToRight' in (root/'ios/GuessTheDrawing/DrawingBoard.swift').read_text()
assert '.environment(\\.layoutDirection, language.direction)' in (root/'ios/GuessTheDrawing/Views.swift').read_text()
for code in codes[1:]:
    for key in ['Player profile','Delete message','Server moderation','Send friend request','Room passkey']:
        assert catalog[code][key] != key, (code,key,'unexpected English fallback')
print(f'PASS: {len(keys)} keys x {len(codes)} languages; native resources, permissions, placeholders, bundle inclusion and RTL wiring')
