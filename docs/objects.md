# Built-in LwM2M Objects

The built-in objects module implements the three mandatory LwM2M objects — Security
(0), Server (1) and Device (3) — together with direct provisioning and a typed
application API.

- Public header: `src/include/sirena/lwm2m/objects.h`
- Implementation: `src/lib/sirena_lwm2m_objects.c`
- Tests: `src/tests/objects/`

## Initialization

```c
#include <sirena/lwm2m/objects.h>

sirena_lwm2m_registry_init();
sirena_lwm2m_objects_init();
```

`sirena_lwm2m_objects_init()` registers objects 0, 1 and 3 in the object registry,
creates instance 0 of each and adds their resources with the value type and the
server-facing access mode from the OMNA registry. It is idempotent: calling it
again after `sirena_lwm2m_registry_init()` succeeds without duplicating resources.

`sirena_lwm2m_object_enabled()` returns `true` for object ids 0, 1 and 3 and
`false` for every other id. No other object is implemented in this version.

## Resource sets

Always registered:

| Object | Resources |
| --- | --- |
| Security (0) | 0 LwM2M Server URI (String, R), 1 Bootstrap-Server (Boolean, R), 2 Security Mode (Integer, R), 3 Public Key or Identity (Opaque, R), 4 Server Public Key (Opaque, R), 5 Secret Key (Opaque, R), 10 Short Server ID (Integer, R) |
| Server (1) | 0 Short Server ID (Integer, R), 1 Lifetime (Integer, RW), 6 Notification Storing (Boolean, RW), 7 Binding (String, RW), 8 Registration Update Trigger (execute) |
| Device (3) | 0 Manufacturer (String, R), 1 Model Number (String, R), 2 Serial Number (String, R), 3 Firmware Version (String, R), 4 Reboot (execute), 11 Error Code (Integer, R), 16 Supported Binding and Modes (String, R), 17 Device Type (String, R), 18 Hardware Version (String, R), 19 Software Version (String, R) |

Security resource 10 is optional in the OMNA registry but registered as always
present here: it pairs the Security instance with the Server instance. Security
resource 1 always reads `false` because v1 has no bootstrap server mode.

Resources registered only when the matching Kconfig symbol is enabled:

| Kconfig symbol | Object | Resources |
| --- | --- | --- |
| `CONFIG_SIRENA_LWM2M_OBJECT_SECURITY_OPTIONAL` | Security (0) | 6, 7, 8, 9, 11, 12, 13, 14, 15, 22, 23, 24, 28, 29, 30 |
| `CONFIG_SIRENA_LWM2M_OBJECT_SERVER_OPTIONAL` | Server (1) | 2, 3, 4, 5 |
| `CONFIG_SIRENA_LWM2M_OBJECT_DEVICE_OPTIONAL` | Device (3) | 5, 9, 10, 12, 13, 14, 15, 20, 21 |

Device 13 Current Time is stored as `SIRENA_LWM2M_VALUE_INTEGER` holding seconds
since the Unix epoch.

ObjLnk-typed resources are not implemented in this version: Security 17 OSCORE
Security Mode and Security 26 MQTT Server are not registered, because the value
type enum and the codec have no object link representation and both resources
point at object types that do not exist in v1. Multi-instance resources and
Device 22 ExtDevInfo are not implemented either.

## Application API

Typed setters and getters address a resource by object, instance and resource id:

```c
int sirena_lwm2m_set_string(uint16_t object_id, uint16_t instance_id,
                            uint16_t resource_id, const char *value);
int sirena_lwm2m_get_string(uint16_t object_id, uint16_t instance_id,
                            uint16_t resource_id, char *buffer, size_t *length);
int sirena_lwm2m_set_integer(uint16_t object_id, uint16_t instance_id,
                             uint16_t resource_id, int64_t value);
int sirena_lwm2m_get_integer(uint16_t object_id, uint16_t instance_id,
                             uint16_t resource_id, int64_t *value);
int sirena_lwm2m_set_boolean(uint16_t object_id, uint16_t instance_id,
                             uint16_t resource_id, bool value);
int sirena_lwm2m_get_boolean(uint16_t object_id, uint16_t instance_id,
                             uint16_t resource_id, bool *value);
int sirena_lwm2m_set_opaque(uint16_t object_id, uint16_t instance_id,
                            uint16_t resource_id, const void *data, size_t length);
int sirena_lwm2m_get_opaque(uint16_t object_id, uint16_t instance_id,
                            uint16_t resource_id, void *buffer, size_t *length);
```

`sirena_lwm2m_get_string()` takes the buffer capacity in `*length` on entry and
returns the character count without the terminating NUL on success. The buffer
always receives a NUL terminator. `sirena_lwm2m_get_opaque()` takes the buffer
capacity in `*length` on entry and returns the stored byte count on success.

The built-in integer resources use a byte width of 8 when read through the typed
API: `sirena_lwm2m_get_integer()` fills an `int64_t` and the server path
returns the same 8 bytes for an integer resource. The LwM2M codec encodes
integers as 4 bytes, so the transport binding narrows the value when it encodes
it.

### Two write paths

The application path and the server path reach the same stored value but enforce
different rules.

The **application path** is the typed setters above. It writes values the
application owns — device identity and metadata, server account parameters and
credentials — and may write a resource whose server-facing access mode is
read-only, because the server may not write those but the application must be
able to supply them. It dispatches through the registered write callback of the
resource, so custom resources behave the same way.

The **server path** is `sirena_lwm2m_set()` and `sirena_lwm2m_execute()` from
`src/include/sirena/lwm2m/object_registry.h`. It stays access-mode enforcing: a
server write to a read-only resource fails with `-ENOTSUP` and an execute-only
resource is not writable. The transport binding uses this path.

### Resource lookup

`sirena_lwm2m_find_resource()` from
`src/include/sirena/lwm2m/object_registry.h` returns the registered resource of
an object instance together with its value type, its server-facing access mode
and its callbacks. It is the lookup the typed accessors use to reach the
resource that stores a value.

### Instance iteration

```c
typedef int (*sirena_lwm2m_instance_visitor_t)(uint16_t object_id, uint16_t instance_id,
                                               void *user_data);

int sirena_lwm2m_foreach_instance(sirena_lwm2m_instance_visitor_t visitor,
                                  void *user_data);
```

`sirena_lwm2m_foreach_instance()` calls the visitor for every object instance
the registry holds, in the order the instances were created. It stops at the
first error the visitor returns and reports it. It returns `-EINVAL` for a
`NULL` visitor. The transport binding uses it to build the object list of the
registration payload.

### Execute handler

```c
typedef void (*sirena_lwm2m_execute_handler_t)(uint16_t object_id,
                                               uint16_t instance_id,
                                               uint16_t resource_id,
                                               void *user_data);

int sirena_lwm2m_set_execute_handler(sirena_lwm2m_execute_handler_t handler,
                                     void *user_data);
```

The built-in execute-only resources — Device 4 Reboot, Device 12 Reset Error
Code, Server 4 Disable and Server 8 Registration Update Trigger — call the
registered handler. Executing one with no handler registered returns `-ENOTSUP`.

### Direct provisioning

```c
enum sirena_lwm2m_security_mode {
    SIRENA_LWM2M_SECURITY_MODE_PSK = 0,
    SIRENA_LWM2M_SECURITY_MODE_RPK = 1,
    SIRENA_LWM2M_SECURITY_MODE_CERTIFICATE = 2,
    SIRENA_LWM2M_SECURITY_MODE_NOSEC = 3,
};

struct sirena_lwm2m_provisioning {
    const char *server_uri;
    uint8_t security_mode;
    const void *identity;
    size_t identity_length;
    const void *server_public_key;
    size_t server_public_key_length;
    const void *secret_key;
    size_t secret_key_length;
    uint16_t short_server_id;
    uint32_t lifetime;
    const char *binding;
};

int sirena_lwm2m_provision(const struct sirena_lwm2m_provisioning *provisioning);
```

`sirena_lwm2m_provision()` fills Security (0) instance 0 and Server (1) instance 0.
The short server id written to Security resource 10 and to Server resource 0 is
the same value, so the Security instance pairs with the Server instance. Security
modes 0 to 3 are supported; any other value is rejected with `-EINVAL`. There is
no bootstrap field because bootstrap is excluded from this version, and Security
resource 1 stays `false`.

## Thread safety

The objects API shares the thread-safety contract of the object registry: it is
not thread-safe and must be called from the engine thread.

## Storage

All built-in values live in static storage; the module performs no heap
allocation and has no heap use in steady state. The buffer capacities come from
Kconfig:

| Kconfig symbol | Purpose | Default |
| --- | --- | --- |
| `CONFIG_SIRENA_LWM2M_SERVER_URI_MAX` | LwM2M server URI length | 64 |
| `CONFIG_SIRENA_LWM2M_CREDENTIAL_MAX` | Identity, server public key, secret key size | 32 |
| `CONFIG_SIRENA_LWM2M_SERVER_STRING_MAX` | Server-side strings such as the binding | 32 |
| `CONFIG_SIRENA_LWM2M_DEVICE_STRING_MAX` | Device metadata strings | 32 |

`CONFIG_SIRENA_LWM2M_MAX_RESOURCES_PER_INSTANCE` defaults to 16 and rises to 25
when `CONFIG_SIRENA_LWM2M_OBJECT_SECURITY_OPTIONAL` or
`CONFIG_SIRENA_LWM2M_OBJECT_DEVICE_OPTIONAL` is enabled, because those
configurations need more resources per instance: the Security instance holds 22
resources and the Device instance 19 with their optional sets enabled, the
Server instance 9.

## Error codes

| Code | Meaning |
| --- | --- |
| `-EINVAL` | The typed setter does not match the registered value type, an argument is invalid, or the provisioning security mode is unsupported. |
| `-ENOENT` | The object, instance or resource does not exist. |
| `-ENOMEM` | The value does not fit the configured capacity of the target buffer. |
| `-ENOTSUP` | The operation is not supported for the resource: a server write to a read-only resource, a write to an execute-only resource, or an execute with no handler registered. |

## Tests

```sh
npm run test:objects
npm run test:objects-optional
```

`npm run test:objects` builds the default configuration, where the optional
resources are absent. `npm run test:objects-optional` builds the same test
sources with the optional-resource symbols enabled, where they are present.

The suite asserts the registered value type and the server-facing access mode
of every resource against the OMNA registry tables, the exact resource count of
each object, and that the resources excluded from this version stay
unregistered.
