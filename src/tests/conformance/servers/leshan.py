import json
import threading
import time
import urllib.request

from servers import api
from servers.base import HarnessError, ServerAdapter, ServerValue


def object_ids_of_links(links):
    object_ids = []
    for link in links:
        url = link.get("url", "")
        parts = url.split("/")
        if len(parts) < 2 or not parts[1].isdigit():
            continue
        object_id = int(parts[1])
        if object_id not in object_ids:
            object_ids.append(object_id)
    return sorted(object_ids)


class LeshanServer(ServerAdapter):
    name = "Leshan"

    def __init__(self, base_url="http://127.0.0.1:8080"):
        self.base_url = base_url
        self.endpoint = None
        self.stream = None

    def wait_until_ready(self):
        deadline = time.time() + 180
        while time.time() < deadline:
            try:
                status, _, _ = api.request_json(self.base_url, "GET", "/api/clients")
                if status == 200:
                    return
            except OSError:
                pass
            time.sleep(0.5)
        raise HarnessError("the Leshan server did not answer on its REST API")

    def wait_for_registration(self, endpoint, timeout):
        self.endpoint = endpoint
        deadline = time.time() + timeout
        while time.time() < deadline:
            status, document, _ = api.request_json(self.base_url, "GET",
                                                   "/api/clients/" + endpoint)
            if status == 200 and document is not None:
                links = document.get("objectLinks", [])
                return {
                    "endpoint": document.get("endpoint"),
                    "lifetime": document.get("lifetime"),
                    "version": document.get("lwM2mVersion"),
                    "binding": document.get("bindingMode"),
                    "object_ids": object_ids_of_links(links),
                    "object_instances": [link.get("url") for link in links],
                }
            time.sleep(0.5)
        raise HarnessError("the Leshan server did not register the endpoint " + endpoint)

    def observe(self, path):
        if self.stream is None:
            self.stream = EventStream(self.base_url, self.endpoint)
            self.stream.start()
        status, _, text = api.request_json(self.base_url, "POST",
                                            "/api/clients/" + self.endpoint + path + "/observe")
        if status != 200:
            raise HarnessError("the observation of " + path + " failed with the status " +
                               str(status) + ": " + text)
        return path

    def wait_for_notification(self, observation, path, timeout):
        data = self.stream.find("NOTIFICATION", path, timeout)
        if data is None:
            raise HarnessError("the Leshan server did not receive a notification of " + path)
        document = json.loads(data)
        content = document.get("val", document)
        if not isinstance(content, dict):
            return ServerValue(None, content)
        return ServerValue(content.get("type"), content.get("value"))

    def read(self, path):
        status, document, text = api.request_json(self.base_url, "GET",
                                                   "/api/clients/" + self.endpoint + path)
        if status != 200 or document is None:
            raise HarnessError("the read of " + path + " failed with the status " +
                               str(status) + ": " + text)
        content = document.get("content", {})
        return ServerValue(content.get("type"), content.get("value"))

    def write(self, path, value, value_type):
        payload = {"id": int(path.split("/")[3]), "type": value_type.upper(), "value": value}
        status, document, text = api.request_json(self.base_url, "PUT",
                                                   "/api/clients/" + self.endpoint + path,
                                                   payload)
        if status != 200:
            raise HarnessError("the write of " + path + " failed with the status " +
                               str(status) + ": " + text)
        if document is None or not document.get("success") or \
                not str(document.get("status")).startswith("CHANGED"):
            raise HarnessError("the write of " + path + " was answered with " + repr(document))

    def execute(self, path):
        status, document, text = api.request_json(self.base_url, "POST",
                                                   "/api/clients/" + self.endpoint + path)
        if status != 200:
            raise HarnessError("the execution of " + path + " failed with the status " +
                               str(status) + ": " + text)
        if document is None or not document.get("success") or \
                not str(document.get("status")).startswith("CHANGED"):
            raise HarnessError("the execution of " + path + " was answered with " + repr(document))

    def wait_for_deregistration(self, endpoint, timeout):
        if self.stream is None:
            raise HarnessError("the Leshan server has no event stream to watch")
        if self.stream.find("DEREGISTRATION", endpoint, timeout) is None:
            raise HarnessError("the Leshan server did not report the deregistration")

    def close(self):
        if self.stream is not None:
            self.stream.stop()
            self.stream = None


class EventStream:
    def __init__(self, base_url, endpoint):
        self.base_url = base_url
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
        url = self.base_url + "/api/event?ep=" + self.endpoint
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
