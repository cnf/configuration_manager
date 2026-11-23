# configuration_manager

ESP-IDF component: typed, described settings per module, stored in NVS. Optionally served on a
[pubsub-c](https://github.com/FunShoot/pubsub-c) bus so any task, or a remote controller, can read and change them.

## Two layers

- **`ConfigStore`** (`config_store.hpp`, always built). Each module registers a list of `ConfigEntry`s (key,
  description, format, type, default, unit) under a namespace. Defaults are written for keys NVS does not have yet.
  Values are read and written by direct calls. Thread safe. Depends on FunLog and nvs_flash.
- **`ConfigManager`** (`config_mgr.hpp`, with `CONFIG_CONFIG_MANAGER_PUBSUB`, default on). Owns a `ConfigStore` and
  serves it from its own task on the `config.*` topics, plus the blocking helpers `RegisterConfig()` and
  `GetSettings()`.

## Use

```cpp
#include "config_mgr.hpp"

static ConfigManager config_manager;
config_manager.Start(); // once, early: initialises NVS and starts the task

ConfigEntries entries = {
    ConfigEntry("brightness", "Display brightness", "int:range=[10,100]", 80, "%"),
};
RegisterConfig(&entries, "panel");

auto settings = std::make_shared<SettingsMap>();
GetSettings(settings, "panel");
int brightness = std::get<int>(settings->at("brightness"));
```

Without the bus: create a `ConfigStore`, call `Setup()`, then `RegisterSettings()` / `ReadSetting()` /
`WriteSetting()`.

Keys are NVS keys: at most 15 characters. Formats (`int:range=[a,b]`, `string:len=[,n]`, `enum=[...]`, ...) follow
[configformat.md](https://github.com/FunShoot/protocol/blob/develop/configformat.md); `ConfigStore::ParseValidator()`
parses them.

## Topics (pubsub front-end)

See `config_topics.hpp`. In short:

| Topic                                          | Payload                     | Effect                                      |
|------------------------------------------------|-----------------------------|---------------------------------------------|
| `config.<ns>.register`                         | PTR `ConfigEntries*`        | register (what `RegisterConfig` sends)      |
| `config.<ns>.get`                              | PTR `shared_ptr<SettingsMap>*` | fill in (what `GetSettings` sends)       |
| `[inc.]config[.<ns>[.<key>]].get`              | anything else               | publish the values as `report.config`       |
| `[inc.]config.<ns>.<key>.set`                  | INT / DBL / BOOL / STR      | write; then `<ns>.config.get` (NIL) + report |
| `[inc.]config.<ns>.<key>.add` / `.remove`      | STR                         | add to / remove from a list; then as set    |
| `[inc.]config.<ns>.<key>.reset`                | any                         | delete the key (default on next register)   |
| `inc.config.format`                            | BOOL true                   | erase NVS, publish `reset`                  |

`report.config` carries a `ConfigReport` buffer (all fields as text), for a transport to put on the wire.
`config.system.get` without a pointer is left to the application (for example a firmware description).

`config_topics.hpp` also defines the generic bus names shared with transports and apps (`kMaxTopicLength`,
`TOPIC_*`, `kTopicTo`, `kTopicReport`, ...). A project's own topic header should include it, not redefine them.

## Kconfig

- `CONFIG_MANAGER_PUBSUB`: build the bus front-end. pubsub-c stays a dependency either way (ESP-IDF fixes
  component requirements before sdkconfig exists), but nothing of it is linked in when this is off.

## Known gaps

- No `config.list`: send `config.get` and group the `report.config` replies by `module_name`.

## License

Apache 2.0 (`LICENSE`). `src/preferences.*` is modified from Espressif's arduino-esp32 Preferences library, also
Apache 2.0.
