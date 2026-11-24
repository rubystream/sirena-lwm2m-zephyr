import json
import urllib.error
import urllib.request


def request(base_url, method, path, payload=None, timeout=20):
    body = None
    headers = {}
    if payload is not None:
        body = json.dumps(payload).encode("utf-8")
        headers["Content-Type"] = "application/json"
    call = urllib.request.Request(base_url + path, data=body, headers=headers, method=method)
    try:
        with urllib.request.urlopen(call, timeout=timeout) as response:
            return response.status, response.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as error:
        return error.code, error.read().decode("utf-8", "replace")


def request_json(base_url, method, path, payload=None, timeout=20):
    status, text = request(base_url, method, path, payload, timeout)
    try:
        document = json.loads(text)
    except ValueError:
        document = None
    return status, document, text
