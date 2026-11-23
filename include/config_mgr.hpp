/**
 * @file config_mgr.hpp
 * @brief The pubsub front-end of the configuration store (CONFIG_CONFIG_MANAGER_PUBSUB).
 *
 * ConfigManager owns a ConfigStore and serves it on the bus from its own task: modules register their entries and
 * fetch their settings with RegisterConfig / GetSettings, and anything on the bus can list, read and change values
 * through the config.* topics (see config_topics.hpp).
 */
#pragma once

#include "sdkconfig.h"

#include "config_store.hpp"
#include "config_topics.hpp"

#if CONFIG_CONFIG_MANAGER_PUBSUB

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

extern "C" {
#include <pubsub.h>
}

/// @brief One setting as published on report.config (a PS_PUB_BUF the subscriber does not own; values as text).
struct ConfigReport {
  std::string module_name;
  std::string key;
  std::string description;
  std::string format;
  std::string value;
  std::string default_value;
  std::string unit;
};

enum TopicVerbs { kVerbList, kVerbGet, kVerbSet, kVerbErase };
struct TopicParts {
  bool incoming = false;
  std::string target; // probably "config" as it is us.
  std::string module; // NVS namespace
  std::string name;   // config key
  TopicVerbs verb = kVerbGet;

  size_t TopicGet(char *buf) {
    snprintf(buf, kMaxTopicLength, "%s.%s", module.c_str(), kTopicConfigGet);
    return strlen(buf);
  };
};

class ConfigManager {
public:
  /// @brief Set up the store and the bus subscriber, then start the task serving them.
  void Start();
  void Loop();
  /// @brief The store behind the bus, for direct calls (thread safe).
  ConfigStore &Store() { return _store; }

  void SendPref(std::string module_name, std::string key);
  void SendPrefs(std::string module_name);
  void SendPrefs();

private:
  ConfigStore _store;
  ps_subscriber_t *_config_sub = nullptr;
  TaskHandle_t _core_task_handle = nullptr;

  void Setup();
  uint32_t ParseTopic(const char *topic, TopicParts *parts);
};

/// @brief Register a module's entries with the ConfigManager (blocks until it did, up to 10 s).
void RegisterConfig(ConfigEntries *config_entries, const char *name);
/// @brief Fetch a module's current settings into `config` (blocks until done, up to 10 s).
void GetSettings(std::shared_ptr<SettingsMap> &config, const char *name);

#endif // CONFIG_CONFIG_MANAGER_PUBSUB
