class HarnessError(Exception):
    pass


class ServerValue:
    def __init__(self, value_type, value):
        self.type = value_type
        self.value = value


class ServerAdapter:
    name = "server"

    def wait_until_ready(self):
        raise NotImplementedError

    def wait_for_registration(self, endpoint, timeout):
        raise NotImplementedError

    def observe(self, path):
        raise NotImplementedError

    def wait_for_notification(self, observation, path, timeout):
        raise NotImplementedError

    def read(self, path):
        raise NotImplementedError

    def write(self, path, value, value_type):
        raise NotImplementedError

    def execute(self, path):
        raise NotImplementedError

    def wait_for_deregistration(self, endpoint, timeout):
        raise NotImplementedError

    def close(self):
        pass
