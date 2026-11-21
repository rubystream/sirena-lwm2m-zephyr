import argparse
import os
import subprocess
import sys
import time

from servers.base import HarnessError
from servers.leshan import LeshanServer
from servers.sirena import SirenaServer

BUILD_ROOT = "/opt/conformance-build"
WEST_DIR = "/opt/zephyr-workspace"
CCACHE_DIR = "/opt/ccache"
BUILD_USER = "user"
SOURCE_DIR = "/workspace/src/tests/conformance"
TAP_DEVICE = "zeth"
CLIENT_LOG = "/tmp/conformance-client.log"
SERVER_OBJECT_ID = 1
DEVICE_OBJECT_ID = 3
INSTANCE_ID = 0
BATTERY_RESOURCE_ID = 9
UTC_OFFSET_RESOURCE_ID = 14
REBOOT_RESOURCE_ID = 4
CONFIG_PREFIXES = ("CONFIG_SIRENA_LWM2M_CONFORMANCE_", "CONFIG_SIRENA_LWM2M_LWM2M_VERSION")

TIERS = {
    "leshan": {
        "server": LeshanServer,
        "extra_conf": SOURCE_DIR + "/prj-leshan.conf",
        "version": "1.1",
    },
    "sirena": {
        "server": SirenaServer,
        "extra_conf": SOURCE_DIR + "/prj-sirena.conf",
        "version": "1.2",
    },
}


def report(message):
    print("harness: " + message, flush=True)


def resolved_config(build_dir):
    values = {}
    path = build_dir + "/zephyr/.config"

    try:
        with open(path, "r", encoding="utf-8") as handle:
            content = handle.read()
    except OSError as error:
        raise HarnessError("the resolved configuration is not readable: " + str(error))

    for line in content.splitlines():
        line = line.strip()
        if not line.startswith(CONFIG_PREFIXES):
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


def prepare_directories():
    subprocess.run(["chown", "-R", BUILD_USER + ":" + BUILD_USER, BUILD_ROOT, CCACHE_DIR],
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


def build_client(tier, build_dir):
    prepare_directories()
    ensure_workspace()
    report("the client is built")
    west(["build", "-b", "native_sim", SOURCE_DIR, "-d", build_dir,
          "--extra-conf", tier["extra_conf"]])


def start_client(build_dir):
    handle = open(CLIENT_LOG, "wb")
    return subprocess.Popen([build_dir + "/zephyr/zephyr.exe", "--rt"], cwd=build_dir,
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


def check_registration(registration, endpoint, lifetime, version, binding):
    checks = [
        ("the endpoint name", registration.get("endpoint"), endpoint),
        ("the lifetime", registration.get("lifetime"), lifetime),
        ("the LwM2M version", registration.get("version"), version),
        ("the binding mode", registration.get("binding"), binding),
    ]
    for name, value, expected in checks:
        if str(value) != str(expected):
            raise HarnessError("the registration carries " + name + " " + repr(value) +
                               " instead of " + repr(expected))
    objects = sorted(int(object_id) for object_id in registration.get("object_ids", []))
    if objects != [SERVER_OBJECT_ID, DEVICE_OBJECT_ID]:
        raise HarnessError("the registration carries the objects " + repr(objects) +
                           " instead of " + repr([SERVER_OBJECT_ID, DEVICE_OBJECT_ID]))
    instances = registration.get("object_instances")
    if instances is not None:
        for link in ("/" + str(SERVER_OBJECT_ID) + "/0", "/" + str(DEVICE_OBJECT_ID) + "/0"):
            if link not in instances:
                raise HarnessError("the registration does not carry the link " + link)
    report("the registration carries the endpoint, the lifetime, the version, the binding "
           "and the object links")


def check_value(name, value, expected, expected_type):
    if str(value.value) != str(expected):
        raise HarnessError(name + " carries the value " + repr(value.value) + " instead of " +
                           repr(expected))
    if value.type is None:
        raise HarnessError(name + " carries no type")
    if str(value.type).lower() != expected_type:
        raise HarnessError(name + " carries the type " + repr(value.type) + " instead of " +
                           repr(expected_type))


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


def run_tier(name):
    tier = TIERS[name]
    server = tier["server"]()
    build_dir = BUILD_ROOT + "/" + name
    process = None
    try:
        server.wait_until_ready()
        report("the " + server.name + " server is ready")
        build_client(tier, build_dir)
        config = resolved_config(build_dir)
        endpoint = config_string(config, "CONFIG_SIRENA_LWM2M_CONFORMANCE_ENDPOINT_NAME")
        server_uri = config_string(config, "CONFIG_SIRENA_LWM2M_CONFORMANCE_SERVER_URI")
        telemetry_value = config_int(config, "CONFIG_SIRENA_LWM2M_CONFORMANCE_TELEMETRY_VALUE")
        write_value = config_string(config, "CONFIG_SIRENA_LWM2M_CONFORMANCE_WRITE_VALUE")
        lifetime = config_int(config, "CONFIG_SIRENA_LWM2M_CONFORMANCE_LIFETIME")
        version = config_string(config, "CONFIG_SIRENA_LWM2M_LWM2M_VERSION")
        if version != tier["version"]:
            raise HarnessError("the client announces the LwM2M version " + repr(version) +
                               " instead of " + repr(tier["version"]))
        report("the client announces the LwM2M version " + version)
        binding = config_string(config, "CONFIG_SIRENA_LWM2M_CONFORMANCE_BINDING")
        host = server_uri.split("//")[1].split(":")[0]
        tap_address = host + "/24"
        battery_path = "/" + str(DEVICE_OBJECT_ID) + "/" + str(INSTANCE_ID) + "/" + \
            str(BATTERY_RESOURCE_ID)
        utc_offset_path = "/" + str(DEVICE_OBJECT_ID) + "/" + str(INSTANCE_ID) + "/" + \
            str(UTC_OFFSET_RESOURCE_ID)
        reboot_path = "/" + str(DEVICE_OBJECT_ID) + "/" + str(INSTANCE_ID) + "/" + \
            str(REBOOT_RESOURCE_ID)
        process = start_client(build_dir)
        configure_tap(tap_address)
        registration = server.wait_for_registration(endpoint, 120)
        report("the " + server.name + " server registered the endpoint " + endpoint)
        check_registration(registration, endpoint, lifetime, tier["version"], binding)
        observation = server.observe(battery_path)
        report("the " + server.name + " server observes " + battery_path)
        value = server.wait_for_notification(observation, battery_path, 60)
        check_value("the notification of " + battery_path, value, telemetry_value, "integer")
        report("the " + server.name + " server received the notification of " + battery_path +
               " with the value " + str(telemetry_value))
        value = server.read(battery_path)
        check_value("the read of " + battery_path, value, telemetry_value, "integer")
        report("the " + server.name + " server read " + battery_path + " and got the value " +
               str(telemetry_value))
        server.write(utc_offset_path, write_value, "string")
        report("the " + server.name + " server wrote " + utc_offset_path + " and the client answered")
        value = server.read(utc_offset_path)
        check_value("the read of " + utc_offset_path, value, write_value, "string")
        report("the " + server.name + " server read back " + utc_offset_path + " and got the value " +
               repr(write_value))
        server.execute(reboot_path)
        report("the " + server.name + " server executed " + reboot_path + " and the client answered")
        code = wait_for_client(process, 120)
        process = None
        if code != 0:
            raise HarnessError("the client stopped with the code " + str(code))
        server.wait_for_deregistration(endpoint, 30)
        report("the " + server.name + " server reported the deregistration")
        report("the client stopped with the code 0")
        return 0
    finally:
        server.close()
        if process is not None and process.poll() is None:
            process.kill()
        remove_tap()
        print_client_log()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--tier", choices=sorted(TIERS),
                        default=os.environ.get("SIRENA_CONFORMANCE_TIER", "leshan"))
    arguments = parser.parse_args()
    try:
        return run_tier(arguments.tier)
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


if __name__ == "__main__":
    sys.exit(main())
