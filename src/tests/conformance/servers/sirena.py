import time

from servers import api
from servers.base import HarnessError, ServerAdapter, ServerValue


class SirenaServer(ServerAdapter):
    name = "Sirena"

    def __init__(self, base_url="http://127.0.0.1:8080"):
        self.base_url = base_url
        self.client_id = None

    def wait_until_ready(self):
        deadline = time.time() + 180
        while time.time() < deadline:
            try:
                status, _, _ = api.request_json(self.base_url, "GET", "/health")
                if status == 200:
                    return
            except OSError:
                pass
            time.sleep(0.5)
        raise HarnessError("the Sirena server did not answer on its REST API")

    def wait_for_registration(self, endpoint, timeout):
        deadline = time.time() + timeout
        while time.time() < deadline:
            client = self._find_client(endpoint)
            if client is not None:
                self.client_id = client.get("id")
                return {
                    "endpoint": client.get("endpointName"),
                    "lifetime": client.get("lifetimeSeconds"),
                    "version": client.get("enablerVersion"),
                    "binding": client.get("bindingMode"),
                    "object_ids": [int(object_id) for object_id in client.get("objectList", [])],
                    "object_instances": None,
                }
            time.sleep(0.5)
        raise HarnessError("the Sirena server did not register the endpoint " + endpoint)

    def observe(self, path):
        status, document, text = api.request_json(self.base_url, "POST",
                                                   "/api/clients/" + self.client_id + "/observe",
                                                   {"path": path})
        if status != 202 or document is None or document.get("observationId") is None:
            raise HarnessError("the observation of " + path + " failed with the status " +
                               str(status) + ": " + text)
        return document.get("observationId")

    def wait_for_notification(self, observation, path, timeout):
        deadline = time.time() + timeout
        while time.time() < deadline:
            status, document, _ = api.request_json(
                self.base_url, "GET",
                "/api/clients/" + self.client_id + "/observations/" + observation +
                "/notifications")
            if status == 200 and document is not None:
                for notification in document.get("notifications", []):
                    if notification.get("path") != path:
                        continue
                    value = notification.get("value", {})
                    if isinstance(value, dict):
                        return ServerValue(value.get("type"), value.get("value"))
                    return ServerValue(None, value)
            time.sleep(0.2)
        raise HarnessError("the Sirena server did not receive a notification of " + path)

    def read(self, path):
        status, document, text = api.request_json(self.base_url, "POST",
                                                   "/api/clients/" + self.client_id + "/read",
                                                   {"path": path})
        if status != 200 or document is None:
            raise HarnessError("the read of " + path + " failed with the status " +
                               str(status) + ": " + text)
        value = document.get("value", {})
        if isinstance(value, dict):
            return ServerValue(value.get("type"), value.get("value"))
        return ServerValue(None, value)

    def write(self, path, value, value_type):
        payload = {"path": path, "value": {"type": value_type, "value": value}}
        status, _, text = api.request_json(self.base_url, "POST",
                                            "/api/clients/" + self.client_id + "/write", payload)
        if status != 204:
            raise HarnessError("the write of " + path + " failed with the status " +
                               str(status) + ": " + text)

    def execute(self, path):
        status, _, text = api.request_json(self.base_url, "POST",
                                            "/api/clients/" + self.client_id + "/execute",
                                            {"path": path})
        if status != 204:
            raise HarnessError("the execution of " + path + " failed with the status " +
                               str(status) + ": " + text)

    def wait_for_deregistration(self, endpoint, timeout):
        deadline = time.time() + timeout
        while time.time() < deadline:
            if self._find_client(endpoint) is None:
                return
            time.sleep(0.2)
        raise HarnessError("the Sirena server did not report the deregistration")

    def _find_client(self, endpoint):
        status, document, _ = api.request_json(self.base_url, "GET", "/api/clients")
        if status != 200 or document is None:
            return None
        for client in document:
            if client.get("endpointName") == endpoint:
                return client
        return None
