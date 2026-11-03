"""Regenerate the checked-in Xcode project without XcodeGen or CocoaPods."""
from pathlib import Path
import hashlib
import json
import struct
import zlib

root = Path(__file__).resolve().parents[1] / "ios"
def ident(name):
    return hashlib.sha256(name.encode()).hexdigest()[:24].upper()
def quote(value):
    return json.dumps(str(value))
objects = {}
def add(name, text):
    objects[ident(name)] = text
    return ident(name)
sources = sorted((root / "GuessTheDrawing").rglob("*.swift"))
references, builds = [], []
for path in sources:
    relative = path.relative_to(root).as_posix()
    ref = add(relative, f'{{isa = PBXFileReference; lastKnownFileType = sourcecode.swift; path = {quote(relative)}; sourceTree = "<group>"; }}')
    references.append(ref)
    builds.append(add("build:" + relative, f'{{isa = PBXBuildFile; fileRef = {ref}; }}'))
flags = add("flags", '{isa = PBXFileReference; lastKnownFileType = folder; path = "../../country-flags"; sourceTree = "<group>"; }')
assets = add("assets", '{isa = PBXFileReference; lastKnownFileType = folder.assetcatalog; path = GuessTheDrawing/Assets.xcassets; sourceTree = "<group>"; }')
config = add("config", '{isa = PBXFileReference; lastKnownFileType = text.xcconfig; path = "../GameVersion.xcconfig"; sourceTree = "<group>"; }')
info = add("info", '{isa = PBXFileReference; lastKnownFileType = text.plist.xml; path = GuessTheDrawing/Info.plist; sourceTree = "<group>"; }')
resources = [add("build:flags", f'{{isa = PBXBuildFile; fileRef = {flags}; }}'), add("build:assets", f'{{isa = PBXBuildFile; fileRef = {assets}; }}')]
product = add("product", '{isa = PBXFileReference; explicitFileType = wrapper.application; path = GuessTheDrawing.app; sourceTree = BUILT_PRODUCTS_DIR; }')
def array(values):
    return "(" + ", ".join(values) + ",)"
group = add("group", f'{{isa = PBXGroup; children = {array(references + [flags, assets, config, info, product])}; sourceTree = "<group>"; }}')
source_phase = add("sourcePhase", f'{{isa = PBXSourcesBuildPhase; buildActionMask = 2147483647; files = {array(builds)}; runOnlyForDeploymentPostprocessing = 0; }}')
resource_phase = add("resourcePhase", f'{{isa = PBXResourcesBuildPhase; buildActionMask = 2147483647; files = {array(resources)}; runOnlyForDeploymentPostprocessing = 0; }}')
framework_phase = add("frameworkPhase", '{isa = PBXFrameworksBuildPhase; buildActionMask = 2147483647; files = (); runOnlyForDeploymentPostprocessing = 0; }')
target_configs, project_configs = [], []
for name in ("Debug", "Release"):
    target_settings = {
        "ASSETCATALOG_COMPILER_APPICON_NAME": "AppIcon",
        "ASSETCATALOG_COMPILER_GLOBAL_ACCENT_COLOR_NAME": "AccentColor",
        "CODE_SIGN_STYLE": "Automatic", "CURRENT_PROJECT_VERSION": "2",
        "GENERATE_INFOPLIST_FILE": "NO", "INFOPLIST_FILE": "GuessTheDrawing/Info.plist",
        "IPHONEOS_DEPLOYMENT_TARGET": "16.0", "MARKETING_VERSION": "$(GTD_GAME_VERSION)",
        "PRODUCT_BUNDLE_IDENTIFIER": "com.guessthedrawing.mobile",
        "PRODUCT_NAME": "$(TARGET_NAME)", "SDKROOT": "iphoneos",
        "SUPPORTED_PLATFORMS": "iphoneos iphonesimulator", "SUPPORTS_MACCATALYST": "NO",
        "SWIFT_VERSION": "5.0", "TARGETED_DEVICE_FAMILY": "1,2",
        "LD_RUNPATH_SEARCH_PATHS": "$(inherited) @executable_path/Frameworks",
        "SWIFT_OPTIMIZATION_LEVEL": "-Onone" if name == "Debug" else "-O",
        "ENABLE_TESTABILITY": "YES" if name == "Debug" else "NO",
    }
    settings = " ".join(f"{k} = {quote(v)};" for k, v in target_settings.items())
    target_configs.append(add("target:" + name, f'{{isa = XCBuildConfiguration; baseConfigurationReference = {config}; buildSettings = {{ {settings} }}; name = {name}; }}'))
    project_configs.append(add("project:" + name, f'{{isa = XCBuildConfiguration; buildSettings = {{ CLANG_ENABLE_MODULES = YES; }}; name = {name}; }}'))
def config_list(name, configurations):
    return add(name, f'{{isa = XCConfigurationList; buildConfigurations = {array(configurations)}; defaultConfigurationIsVisible = 0; defaultConfigurationName = Release; }}')
target_config = config_list("targetConfig", target_configs)
project_config = config_list("projectConfig", project_configs)
target = add("target", f'{{isa = PBXNativeTarget; buildConfigurationList = {target_config}; buildPhases = {array([source_phase, framework_phase, resource_phase])}; buildRules = (); dependencies = (); name = GuessTheDrawing; productName = GuessTheDrawing; productReference = {product}; productType = "com.apple.product-type.application"; }}')
project = add("project", f'{{isa = PBXProject; attributes = {{ LastUpgradeCheck = 1600; }}; buildConfigurationList = {project_config}; compatibilityVersion = "Xcode 14.0"; developmentRegion = en; hasScannedForEncodings = 0; knownRegions = (en, Base); mainGroup = {group}; productRefGroup = {group}; projectDirPath = ""; projectRoot = ""; targets = {array([target])}; }}')
directory = root / "GuessTheDrawing.xcodeproj"
directory.mkdir(parents=True, exist_ok=True)
(directory / "project.pbxproj").write_text(
    "// !$*UTF8*$!\n{ archiveVersion = 1; classes = {}; objectVersion = 56; objects = {\n"
    + "\n".join(f"{key} = {value};" for key, value in objects.items()) + f"\n}}; rootObject = {project}; }}\n", encoding="utf-8")
scheme = directory / "xcshareddata" / "xcschemes" / "GuessTheDrawing.xcscheme"
scheme.parent.mkdir(parents=True, exist_ok=True)
reference = f'<BuildableReference BuildableIdentifier="primary" BlueprintIdentifier="{target}" BuildableName="GuessTheDrawing.app" BlueprintName="GuessTheDrawing" ReferencedContainer="container:GuessTheDrawing.xcodeproj"/>'
scheme.write_text(f'''<?xml version="1.0" encoding="UTF-8"?>
<Scheme LastUpgradeVersion="1600" version="1.3">
  <BuildAction parallelizeBuildables="YES" buildImplicitDependencies="YES"><BuildActionEntries><BuildActionEntry buildForTesting="YES" buildForRunning="YES" buildForProfiling="YES" buildForArchiving="YES" buildForAnalyzing="YES">{reference}</BuildActionEntry></BuildActionEntries></BuildAction>
  <TestAction buildConfiguration="Debug" shouldUseLaunchSchemeArgsEnv="YES"><Testables/></TestAction>
  <LaunchAction buildConfiguration="Debug" selectedDebuggerIdentifier="Xcode.DebuggerFoundation.Debugger.LLDB" selectedLauncherIdentifier="Xcode.IDEFoundation.Launcher.LLDB" launchStyle="0" useCustomWorkingDirectory="NO" ignoresPersistentStateOnLaunch="NO" debugDocumentVersioning="YES" allowLocationSimulation="YES"><BuildableProductRunnable runnableDebuggingMode="0">{reference}</BuildableProductRunnable></LaunchAction>
  <ProfileAction buildConfiguration="Release" shouldUseLaunchSchemeArgsEnv="YES" savedToolIdentifier="" useCustomWorkingDirectory="NO" debugDocumentVersioning="YES"><BuildableProductRunnable runnableDebuggingMode="0">{reference}</BuildableProductRunnable></ProfileAction>
  <AnalyzeAction buildConfiguration="Debug"/>
  <ArchiveAction buildConfiguration="Release" revealArchiveInOrganizer="YES"/>
</Scheme>
''', encoding="utf-8")

# Procedural pencil icon matching the Android vector; no external asset dependency.
assets_dir = root / "GuessTheDrawing" / "Assets.xcassets"
assets_dir.mkdir(exist_ok=True)
(assets_dir / "Contents.json").write_text('{"info":{"author":"xcode","version":1}}\n')
accent = assets_dir / "AccentColor.colorset"
accent.mkdir(exist_ok=True)
(accent / "Contents.json").write_text(json.dumps({"colors": [{"idiom": "universal", "color": {"color-space": "srgb", "components": {"red": "0.318", "green": "0.275", "blue": "0.847", "alpha": "1.000"}}}], "info": {"author": "xcode", "version": 1}}, indent=2))
icon_dir = assets_dir / "AppIcon.appiconset"
icon_dir.mkdir(exist_ok=True)
polygons = [([(25, 72), (31, 52), (65, 18), (85, 38), (51, 72), (25, 78)], (255, 255, 255)),
            ([(25, 78), (31, 58), (45, 72)], (255, 207, 105))]
def inside(x, y, polygon):
    result = False
    for a, b in zip(polygon, polygon[1:] + polygon[:1]):
        if (a[1] > y) != (b[1] > y) and x < (b[0] - a[0]) * (y - a[1]) / (b[1] - a[1]) + a[0]:
            result = not result
    return result
rows = bytearray()
for y in range(1024):
    rows.append(0)
    for x in range(1024):
        color = (81, 70, 216)
        for polygon, fill in polygons:
            if inside(x * 108 / 1024, y * 108 / 1024, polygon):
                color = fill
        rows.extend(color)
def chunk(name, content):
    return struct.pack(">I", len(content)) + name + content + struct.pack(">I", zlib.crc32(name + content) & 0xffffffff)
(icon_dir / "AppIcon.png").write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 1024, 1024, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))
(icon_dir / "Contents.json").write_text(json.dumps({"images": [{"filename": "AppIcon.png", "idiom": "universal", "platform": "ios", "size": "1024x1024"}], "info": {"author": "xcode", "version": 1}}, indent=2))
print(directory)
