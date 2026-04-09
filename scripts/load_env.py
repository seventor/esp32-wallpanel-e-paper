from pathlib import Path
from SCons.Script import Import

Import("env")


def parse_dotenv(path: Path):
    data = {}
    if not path.exists():
        return data
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        key = key.strip()
        value = value.strip()
        if not key:
            continue
        if len(value) >= 2 and value[0] == value[-1] and value[0] in ("'", '"'):
            value = value[1:-1]
        data[key] = value
    return data


project_dir = Path(env["PROJECT_DIR"])
dotenv = parse_dotenv(project_dir / ".env")

# Inject WiFi + OTA secrets as compile-time defines.
for key in ("WIFI_SSID", "WIFI_PASSWORD", "OTA_PASSWORD"):
    value = dotenv.get(key)
    if value:
        env.Append(CPPDEFINES=[(key, '\\"{}\\"'.format(value.replace("\\", "\\\\").replace('"', '\\"')))])

# Optional OTA runtime settings from .env:
#   ESP32_OTA_IP=192.168.2.2
#   OTA_PASSWORD=...
pioenv = env.get("PIOENV", "")
if pioenv.endswith("_ota"):
    ota_ip = dotenv.get("ESP32_OTA_IP")
    if ota_ip:
        env.Replace(UPLOAD_PORT=ota_ip)

    ota_password = dotenv.get("OTA_PASSWORD")
    if ota_password:
        upload_flags = list(env.get("UPLOAD_FLAGS", []))
        upload_flags = [f for f in upload_flags if not str(f).startswith("--auth=")]
        upload_flags.append(f"--auth={ota_password}")
        env.Replace(UPLOAD_FLAGS=upload_flags)
