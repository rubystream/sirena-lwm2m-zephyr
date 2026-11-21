import json
import os
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request

CONFIG_PATH = "/opt/leshan-build/zephyr/.config"
BUILD_DIR = "/opt/leshan-build"
WEST_DIR = "/opt/zephyr-workspace"
CCACHE_DIR = "/opt/ccache"
BUILD_USER = "user"
LESHAN_API = "http://127.0.0.1:8080"
TAP_DEVICE = "zeth"
CLIENT_LOG = "/tmp/leshan-client.log"
DEVICE_OBJECT_ID = 3
INSTANCE_ID = 0
BATTERY_RESOURCE_ID = 9
UTC_OFFSET_RESOURCE_ID = 14
REBOOT_RESOURCE_ID = 4


class HarnessError(Exception):
    pass


def report(message):
    print("harness: " + message, flush=True)


def resolved_config():
    values = {}

    try:
        with open(CONFIG_PATH, "r", encoding="utf-8") as handle:
            content = handle.read()
    except OSError as error:
        raise HarnessError("the resolved configuration is not readable: " + str(error))

    for line in content.splitlines():
        line = line.strip()
        if not line.startswith("CONFIG_SIRENA_LWM2M_LESHAN_"):
            continue
        name, separator, value = line.partition("=")
        if not separator:
            continue
        if len(value) > 1 and value.startswith('"') and value.endswith('"'):
            value = value[1:-1]
        values[name] = value

    return values


def config_string(values, name):
    if name not in values:
        raise HarnessError("the resolved configuration does not carry " + name)
    return values[name]


def config_int(values, name):
    value = config_string(values, name)
    if not value.lstrip("-").isdigit():
        raise HarnessError("the resolved configuration carries " + name + " as " + repr(value))
    return int(value)


def http_request(method, path, body=None, timeout=20):
    data = None
    headers = {}
    if body is not None:
        data = body.encode("utf-8")
        headers["Content-Type"] = "application/json"
    request = urllib.request.Request(LESHAN_API + path, data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            return response.status, response.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as error:
        return error.code, error.read().decode("utf-8", "replace")


def http_json(method, path, payload=None):
    body = None
    if payload is not None:
        body = json.dumps(payload)
    status, text = http_request(method, path, body)
    try:
        document = json.loads(text)
    except ValueError:
        document = None
    return status, document, text


def wait_for_server():
    deadline = time.time() + 180
    while time.time() < deadline:
        try:
            status, _, _ = http_json("GET", "/api/clients")
            if status == 200:
                report("the Leshan server answers on its REST API")
                return
        except OSError:
            pass
        time.sleep(0.5)
    raise HarnessError("the Leshan server did not answer on its REST API")


def prepare_directories():
    subprocess.run(["chown", "-R", BUILD_USER + ":" + BUILD_USER, BUILD_DIR, CCACHE_DIR],
                   check=True)
    subprocess.run(["chown", BUILD_USER + ":" + BUILD_USER, WEST_DIR], check=True)


def west(arguments):
    command = ["runuser", "-u", BUILD_USER, "--", "env", "PATH=" + os.environ["PATH"],
               "HOME=/home/" + BUILD_USER, "CCACHE_DIR=" + CCACHE_DIR, "west"] + arguments
    return subprocess.run(command, cwd=WEST_DIR, check=True)


def ensure_workspace():
    if os.path.isdir("/opt/zephyr-workspace/.west"):
        return
    report("the west workspace is created")
    west(["init", "-l", "/workspace", "/opt/zephyr-workspace"])
    west(["update"])


def build_client():
    prepare_directories()
    ensure_workspace()
    report("the client is built")
    west(["build", "-b", "native_sim", "/workspace/src/tests/leshan", "-d", BUILD_DIR])


def start_client():
    handle = open(CLIENT_LOG, "wb")
    return subprocess.Popen([BUILD_DIR + "/zephyr/zephyr.exe", "--rt"], cwd=BUILD_DIR,
                            stdout=handle, stderr=subprocess.STDOUT)


def tap_present():
    result = subprocess.run(["ip", "link", "show", TAP_DEVICE], capture_output=True, text=True)
    return result.returncode == 0


def configure_tap(address):
    deadline = time.time() + 120
    while time.time() < deadline:
        if tap_present():
            break
        time.sleep(0.05)
    else:
        raise HarnessError("the TAP device " + TAP_DEVICE + " did not appear")
    subprocess.run(["ip", "address", "replace", address, "dev", TAP_DEVICE], check=True)
    subprocess.run(["ip", "link", "set", TAP_DEVICE, "up"], check=True)
    report("the TAP device " + TAP_DEVICE + " carries " + address)


def remove_tap():
    subprocess.run(["ip", "link", "delete", TAP_DEVICE], capture_output=True, text=True)


def wait_for_registration(endpoint):
    deadline = time.time() + 120
    while time.time() < deadline:
        status, document, _ = http_json("GET", "/api/clients/" + endpoint)
        if status == 200 and document is not None:
            report("the Leshan server registered the endpoint " + endpoint)
            return document
        time.sleep(0.5)
    raise HarnessError("the Leshan server did not register the endpoint " + endpoint)


def check_registration(registration, endpoint, lifetime, version, binding):
    links = [link.get("url") for link in registration.get("objectLinks", [])]
    checks = [
        ("the endpoint name", registration.get("endpoint"), endpoint),
        ("the lifetime", registration.get("lifetime"), lifetime),
        ("the LwM2M version", registration.get("lwM2mVersion"), version),
        ("the binding mode", registration.get("bindingMode"), binding),
        ("the root path", registration.get("rootPath"), "/"),
    ]
    for name, value, expected in checks:
        if value != expected:
            raise HarnessError("the registration carries " + name + " " + repr(value) +
                               " instead of " + repr(expected))
    for link in ("/1/0", "/3/0"):
        if link not in links:
            raise HarnessError("the registration does not carry the link " + link)
    report("the registration carries the endpoint, the lifetime, the version, the binding "
           "and the object links")


class EventStream:
    def __init__(self, endpoint):
        self.endpoint = endpoint
        self.events = []
        self.lock = threading.Lock()
        self.running = True
        self.thread = threading.Thread(target=self._read, daemon=True)

    def start(self):
        self.thread.start()
        time.sleep(1.0)

    def stop(self):
        self.running = False

    def _read(self):
        url = LESHAN_API + "/api/event?ep=" + self.endpoint
        request = urllib.request.Request(url, headers={"Accept": "text/event-stream"})
        while self.running:
            try:
                with urllib.request.urlopen(request, timeout=300) as response:
                    name = ""
                    for raw in response:
                        if not self.running:
                            return
                        line = raw.decode("utf-8", "replace").strip()
                        if line.startswith("event:"):
                            name = line[len("event:"):].strip()
                        elif line.startswith("data:"):
                            data = line[len("data:"):].strip()
                            with self.lock:
                                self.events.append((name, data))
            except Exception:
                time.sleep(0.5)

    def find(self, name, path, timeout):
        deadline = time.time() + timeout
        while time.time() < deadline:
            with self.lock:
                for event_name, data in self.events:
                    if event_name == name and path in data:
                        return data
            time.sleep(0.1)
        return None


def observe(endpoint, path):
    status, document, text = http_json("POST", "/api/clients/" + endpoint + path + "/observe")
    if status != 200:
        raise HarnessError("the observation of " + path + " failed with the status " +
                           str(status) + ": " + text)
    report("the Leshan server observes " + path)
    return document


def check_notification(data, path, value):
    document = json.loads(data)
    content = document.get("val", {})
    if str(content.get("value")) != str(value):
        raise HarnessError("the notification of " + path + " carries the value " +
                           repr(content.get("value")) + " instead of " + repr(value))
    report("the Leshan server received the notification of " + path + " with the value " +
           str(value))


def check_read(endpoint, path, value):
    status, document, text = http_json("GET", "/api/clients/" + endpoint + path)
    if status != 200:
        raise HarnessError("the read of " + path + " failed with the status " + str(status) +
                           ": " + text)
    content = document.get("content", {})
    if str(content.get("value")) != str(value):
        raise HarnessError("the read of " + path + " carries the value " +
                           repr(content.get("value")) + " instead of " + repr(value))
    report("the Leshan server read " + path + " and got the value " + str(value))


def check_write(endpoint, path, resource_id, value):
    payload = {"id": resource_id, "type": "STRING", "value": value}
    status, document, text = http_json("PUT", "/api/clients/" + endpoint + path, payload)
    if status != 200:
        raise HarnessError("the write of " + path + " failed with the status " + str(status) +
                           ": " + text)
    if document is None or not document.get("success") or \
            not str(document.get("status")).startswith("CHANGED"):
        raise HarnessError("the write of " + path + " was answered with " + repr(document))
    report("the Leshan server wrote " + path + " and the client answered with a changed status")


def check_written_value(endpoint, path, value):
    status, document, text = http_json("GET", "/api/clients/" + endpoint + path)
    if status != 200:
        raise HarnessError("the read of " + path + " failed with the status " + str(status) +
                           ": " + text)
    content = document.get("content", {})
    if content.get("value") != value:
        raise HarnessError("the read of " + path + " carries the value " +
                           repr(content.get("value")) + " instead of " + repr(value))
    report("the Leshan server read back " + path + " and got the value " + repr(value))


def check_execute(endpoint, path):
    status, document, text = http_json("POST", "/api/clients/" + endpoint + path)
    if status != 200:
        raise HarnessError("the execution of " + path + " failed with the status " +
                           str(status) + ": " + text)
    if document is None or not document.get("success") or \
            not str(document.get("status")).startswith("CHANGED"):
        raise HarnessError("the execution of " + path + " was answered with " + repr(document))
    report("the Leshan server executed " + path + " and the client answered with a changed status")


def check_deregistration(stream, endpoint):
    data = stream.find("DEREGISTRATION", endpoint, 10)
    if data is None:
        raise HarnessError("the Leshan server did not report the deregistration")
    report("the Leshan server reported the deregistration")


def wait_for_client(process, timeout):
    deadline = time.time() + timeout
    while time.time() < deadline:
        code = process.poll()
        if code is not None:
            return code
        time.sleep(0.2)
    process.kill()
    raise HarnessError("the client did not stop in time")


def print_client_log():
    if not os.path.exists(CLIENT_LOG):
        return
    with open(CLIENT_LOG, "r", encoding="utf-8", errors="replace") as handle:
        lines = handle.readlines()
    report("the output of the client follows")
    for line in lines[-40:]:
        print("client: " + line.rstrip(), flush=True)


def main():
    process = None
    stream = None
    try:
        wait_for_server()
        build_client()
        config = resolved_config()
        endpoint = config_string(config, "CONFIG_SIRENA_LWM2M_LESHAN_ENDPOINT_NAME")
        server_uri = config_string(config, "CONFIG_SIRENA_LWM2M_LESHAN_SERVER_URI")
        telemetry_value = config_int(config, "CONFIG_SIRENA_LWM2M_LESHAN_TELEMETRY_VALUE")
        write_value = config_string(config, "CONFIG_SIRENA_LWM2M_LESHAN_WRITE_VALUE")
        lifetime = config_int(config, "CONFIG_SIRENA_LWM2M_LESHAN_LIFETIME")
        version = config_string(config, "CONFIG_SIRENA_LWM2M_LESHAN_LWM2M_VERSION")
        binding = config_string(config, "CONFIG_SIRENA_LWM2M_LESHAN_BINDING")
        host = server_uri.split("//")[1].split(":")[0]
        tap_address = host + "/24"
        battery_path = "/" + str(DEVICE_OBJECT_ID) + "/" + str(INSTANCE_ID) + "/" + \
            str(BATTERY_RESOURCE_ID)
        utc_offset_path = "/" + str(DEVICE_OBJECT_ID) + "/" + str(INSTANCE_ID) + "/" + \
            str(UTC_OFFSET_RESOURCE_ID)
        reboot_path = "/" + str(DEVICE_OBJECT_ID) + "/" + str(INSTANCE_ID) + "/" + \
            str(REBOOT_RESOURCE_ID)
        process = start_client()
        configure_tap(tap_address)
        registration = wait_for_registration(endpoint)
        check_registration(registration, endpoint, lifetime, version, binding)
        stream = EventStream(endpoint)
        stream.start()
        observe(endpoint, battery_path)
        data = stream.find("NOTIFICATION", battery_path, 60)
        if data is None:
            raise HarnessError("the Leshan server did not receive a notification of " +
                               battery_path)
        check_notification(data, battery_path, telemetry_value)
        check_read(endpoint, battery_path, telemetry_value)
        check_write(endpoint, utc_offset_path, UTC_OFFSET_RESOURCE_ID, write_value)
        check_written_value(endpoint, utc_offset_path, write_value)
        check_execute(endpoint, reboot_path)
        code = wait_for_client(process, 120)
        process = None
        if code != 0:
            raise HarnessError("the client stopped with the code " + str(code))
        check_deregistration(stream, endpoint)
        report("the client stopped with the code 0")
        stream.stop()
        return 0
    except HarnessError as error:
        report("the harness failed: " + str(error))
        return 1
    except subprocess.CalledProcessError as error:
        report("the harness failed: the command " + " ".join(error.cmd) + " returned " +
               str(error.returncode))
        return 1
    except Exception as error:
        report("the harness failed: " + type(error).__name__ + ": " + str(error))
        return 1
    finally:
        if stream is not None:
            stream.stop()
        if process is not None and process.poll() is None:
            process.kill()
        remove_tap()
        print_client_log()


if __name__ == "__main__":
    sys.exit(main())
