"""Record and export native iOS game bundles using the shared resource pipeline."""

import argparse
from datetime import datetime, timezone
import fnmatch
import hashlib
import json
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from artifact_output import plain_path, staged_output, validate_output
from resource_catalog import copy_resource_snapshot, parse_build_output, safe_file
from runtime_settings import export_runtime_settings


def run(command):
    result = subprocess.run([str(x) for x in command], capture_output=True,
                            encoding="utf-8", check=False, timeout=600)
    if result.returncode:
        raise ValueError("iOS tool failed: " + str(command[0]) + "\n" + result.stderr)
    return result.stdout


def checksum(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def validate_binary(path, sdk):
    if run(["xcrun", "lipo", "-archs", path]).strip() != "arm64":
        raise ValueError("The iOS executable must contain only arm64")
    build = run(["xcrun", "vtool", "-show-build", path])
    platform = "IOSSIMULATOR" if sdk == "simulator" else "IOS"
    if not re.search(r"\bplatform\s+" + platform + r"\b", build):
        raise ValueError("The executable does not match the selected iOS SDK")
    for line in run(["xcrun", "otool", "-L", path]).splitlines()[1:]:
        library = line.strip().split(" (", 1)[0]
        if library and not library.startswith(("/System/Library/", "/usr/lib/")):
            raise ValueError("The runtime contains an undeployed library: " + library)


def record(args):
    config = json.loads(args.config.read_text(encoding="utf-8"))
    binary = safe_file(args.bundle, config["executable"])
    validate_binary(binary, config["sdk"])
    config["checksums"] = {name: checksum(safe_file(args.bundle, name))
                           for name in (config["executable"], "Info.plist")}
    temporary = args.output.with_suffix(".tmp")
    temporary.write_text(json.dumps(config, indent=2) + "\n", encoding="utf-8")
    temporary.replace(args.output)


def sign_bundle(bundle, args, info, stage):
    if args.sdk == "simulator":
        if args.provisioning_profile or args.sign_identity:
            raise ValueError("The simulator package does not accept device signing credentials")
        run(["codesign", "--force", "--sign", "-", bundle])
        run(["codesign", "--verify", "--strict", bundle])
        return "simulator-ad-hoc"
    if not args.sign_identity:
        if args.provisioning_profile or args.ipa:
            raise ValueError("Device profiles and IPA export require a signing identity")
        return "unsigned"
    if not args.provisioning_profile:
        raise ValueError("Device signing requires a provisioning profile")
    profile_bytes = subprocess.run(
        ["security", "cms", "-D", "-i", str(plain_path(args.provisioning_profile))],
        capture_output=True, check=True).stdout
    profile = plistlib.loads(profile_bytes)
    expiration = profile.get("ExpirationDate")
    if not isinstance(expiration, datetime) or expiration.replace(tzinfo=timezone.utc) <= datetime.now(timezone.utc):
        raise ValueError("The provisioning profile has expired or has no expiration date")
    allowed = profile.get("Entitlements", {})
    team = allowed.get("com.apple.developer.team-identifier", "")
    identifier = team + "." + info["bundleIdentifier"]
    if not team or not fnmatch.fnmatchcase(identifier, allowed.get("application-identifier", "")):
        raise ValueError("The provisioning profile does not cover the bundle identifier")
    entitlements = {"application-identifier": identifier,
                    "com.apple.developer.team-identifier": team,
                    "get-task-allow": bool(allowed.get("get-task-allow", False))}
    if "keychain-access-groups" in allowed:
        entitlements["keychain-access-groups"] = [
            group.replace("*", info["bundleIdentifier"])
            for group in allowed["keychain-access-groups"]]
    path = stage / "signing-entitlements.plist"
    path.write_bytes(plistlib.dumps(entitlements))
    shutil.copy2(args.provisioning_profile, bundle / "embedded.mobileprovision")
    run(["codesign", "--force", "--sign", args.sign_identity,
         "--entitlements", path, "--generate-entitlement-der", bundle])
    run(["codesign", "--verify", "--strict", bundle])
    prefix = stage / "signing-certificate-"
    run(["codesign", "--display", "--extract-certificates", prefix, bundle])
    leaf = stage / "signing-certificate-0"
    if not leaf.is_file() or leaf.read_bytes() not in profile.get("DeveloperCertificates", []):
        raise ValueError("The signing certificate is not authorized by the provisioning profile")
    for certificate in stage.glob("signing-certificate-*"):
        certificate.unlink()
    path.unlink()
    return "device-signed"


def export(args):
    if not re.fullmatch(r"[a-z][a-z0-9_-]*", args.product):
        raise ValueError("Product name must be a lowercase English identifier")
    args.project, args.builder = plain_path(args.project), plain_path(args.builder)
    args.bundle = plain_path(args.bundle)
    info = json.loads(args.receipt.read_text(encoding="utf-8"))
    expected = {"platform": "ios", "arch": "arm64", "sdk": args.sdk,
                "product": args.product, "configuration": args.configuration,
                "testSupport": args.test_package}
    if not isinstance(info, dict) or type(info.get("testSupport")) is not bool or \
            any(info.get(k) != v for k, v in expected.items()):
        raise ValueError("The linked runtime does not match the iOS export configuration")
    plugins = info.get("plugins")
    if not isinstance(plugins, list) or any(not isinstance(p, str) or not p.startswith("runtime/")
                                           for p in plugins):
        raise ValueError("The iOS runtime must not contain editor plugins")
    if ("runtime/mcp" in plugins) != args.test_package:
        raise ValueError("Runtime test endpoints must match the explicit test package flag")
    if info.get("scriptBackend") != "quickjs":
        raise ValueError("The iOS runtime requires the supported QuickJS build configuration")
    executable = info.get("executable")
    if executable != args.product + ("-test" if args.test_package else ""):
        raise ValueError("The iOS executable identity is invalid")
    for name in (executable, "Info.plist"):
        if info.get("checksums", {}).get(name) != checksum(safe_file(args.bundle, name)):
            raise ValueError("The runtime file differs from its successful link receipt: " + name)
    validate_binary(safe_file(args.bundle, executable), args.sdk)
    package_info = plistlib.loads(safe_file(args.bundle, "Info.plist").read_bytes())
    if package_info.get("CFBundleIdentifier") != info.get("bundleIdentifier") or \
            package_info.get("MinimumOSVersion") != info.get("minimumOS"):
        raise ValueError("The application metadata does not match the linked runtime")
    output = validate_output(args.output, ROOT / "output/ios/arm64" / args.configuration / "runtime" / args.sdk,
                             [args.project, args.builder, args.bundle])
    command = [args.builder, "--project", args.project, "--package", "--target", "ios"]
    if args.manifest:
        command += ["--manifest", args.manifest.as_posix()]
    result = subprocess.run([str(x) for x in command], capture_output=True, encoding="utf-8",
                            check=False, timeout=600)
    if result.returncode:
        raise ValueError("Project resource build failed:\n" + result.stdout + "\n" + result.stderr)
    source = plain_path(parse_build_output(result.stdout, "ios"))
    validate_output(output, ROOT / "output", [source])
    with staged_output(output) as stage:
        bundle = stage / (executable + ".app")
        shutil.copytree(args.bundle, bundle)
        shutil.rmtree(bundle / "_CodeSignature", ignore_errors=True)
        (bundle / "assets").mkdir()
        resources = copy_resource_snapshot(source, bundle / "assets", "ios",
                                           carrier_roots=("gen", "packs"),
                                           require_unchanged_catalog=True)
        export_runtime_settings(args.project, bundle, "ios")
        package_info["CFBundleSupportedPlatforms"] = [
            "iPhoneSimulator" if args.sdk == "simulator" else "iPhoneOS"]
        if args.icon:
            shutil.copy2(plain_path(args.icon), bundle / "Assets.car")
            package_info["CFBundleIconName"] = "AppIcon"
        if args.privacy_manifest:
            privacy = plain_path(args.privacy_manifest)
            plistlib.loads(privacy.read_bytes())
            shutil.copy2(privacy, bundle / "PrivacyInfo.xcprivacy")
        (bundle / "Info.plist").write_bytes(plistlib.dumps(package_info))
        licenses = bundle / "licenses"
        licenses.mkdir(exist_ok=True)
        for path in plain_path(args.dependency_licenses).glob("*/copyright"):
            shutil.copy2(path, licenses / (path.parent.name + ".txt"))
        shutil.copytree(ROOT / "third_party/notices", licenses / "engine")
        selected = ["third_party/ozz-animation/LICENSE.md"]
        for plugin, files in {
            "runtime/effects": ["third_party/effekseer/LICENSE", "third_party/efkbgfx/LICENSE"],
            "runtime/terrain": ["third_party/transvoxel/LICENSE"],
            "runtime/destruction": ["third_party/blast/PACKAGE-LICENSES/blast-sdk-LICENSE.md"]
        }.items():
            if plugin in plugins:
                selected += files
        for relative in selected:
            path = ROOT / relative
            shutil.copy2(path, licenses / (path.parent.name + "-" + path.name))
        signing = sign_bundle(bundle, args, info, stage)
        if args.ipa:
            if signing != "device-signed":
                raise ValueError("IPA export requires a signed device application")
            with zipfile.ZipFile(stage / (executable + ".ipa"), "w", zipfile.ZIP_DEFLATED) as archive:
                for path in sorted(bundle.rglob("*")):
                    if path.is_file():
                        archive.write(path, Path("Payload") / bundle.name / path.relative_to(bundle))
        (stage / "ios-build.json").write_text(json.dumps(info, indent=2) + "\n", encoding="utf-8")
        summary = {"platform": "ios", "sdk": args.sdk, "signing": signing,
                   "testSupport": args.test_package, "resourceCount": resources["resource_count"],
                   "revision": resources["revision"],
                   "checksums": {name: checksum(bundle / name)
                                 for name in (executable, "Info.plist")}}
        (stage / "export.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"status": "ok", "output": str(output), **summary}))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    receipt = commands.add_parser("record")
    for name in ("config", "bundle", "output"):
        receipt.add_argument("--" + name, type=Path, required=True)
    package = commands.add_parser("export")
    for name in ("project", "builder", "bundle", "receipt", "output", "dependency-licenses"):
        package.add_argument("--" + name, type=Path, required=True)
    package.add_argument("--product", required=True)
    package.add_argument("--configuration", choices=("debug", "release"), required=True)
    package.add_argument("--sdk", choices=("device", "simulator"), required=True)
    package.add_argument("--manifest", type=Path)
    package.add_argument("--sign-identity")
    package.add_argument("--provisioning-profile", type=Path)
    package.add_argument("--icon", type=Path)
    package.add_argument("--privacy-manifest", type=Path)
    package.add_argument("--test-package", action="store_true")
    package.add_argument("--ipa", action="store_true")
    args = parser.parse_args()
    try:
        (record if args.command == "record" else export)(args)
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
        parser.exit(1, str(error) + "\n")


if __name__ == "__main__":
    main()
