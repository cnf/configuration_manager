/**
 * @file config_mgr.cpp
 * @brief The pubsub front-end of the ConfigStore: the config.* topics and the RegisterConfig / GetSettings calls.
 */
#include "sdkconfig.h"

#if CONFIG_CONFIG_MANAGER_PUBSUB

#include "config_mgr.hpp"
#include <cstdio>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <tuple> // for std::get

#include <funlog.h>

static uint32_t rt_counter = 0;

static void CfgMngrShim(void *pvParameters) {
  while (true) {
    reinterpret_cast<ConfigManager *>(pvParameters)->Loop();
    vTaskDelay(pdMS_TO_TICKS(100)); // wait 100ms so we give other tasks time to do their thing.
  }
  FLOG_ERROR("Config Manager Loop exited, this should never happen!");
  PS_CALL_NIL(kTopicReset, 10000); // reset the MCU if we crash/fail
}

void ConfigManager::Start() {
  FLOG_INFO("Starting Config Manager");
  this->Setup();
  xTaskCreatePinnedToCore(CfgMngrShim,        // Task function
                          "Config Manager",   // name of task
                          8192,               // Stack size of task, in bytes
                          this,               // parameter of the task
                          7,                  // priority of the task
                          &_core_task_handle, // task handle
                          0);                 // MAIN_CPU
};

void ConfigManager::Setup() {
  FLOG_DEBUG("Listening on %s, %s", kTopicConfig, kTopicIncConfig);
  _config_sub = ps_new_subscriber(10, PS_STRLIST(kTopicConfig, kTopicIncConfig));
  _store.Setup();
}

void ConfigManager::Loop() {
  ps_msg_t *msg = NULL;
  TopicParts parts;
  for ((msg = ps_get(_config_sub, 0)); msg != NULL; (msg = ps_get(_config_sub, 0))) {
    parts = TopicParts{};
    ParseTopic(msg->topic, &parts);
    FLOG_DEBUG("Incoming Request: %s", msg->topic);
    if (ps_has_topic(msg, kTopicConfigFormat) && PS_IS_BOOL(msg) && msg->bool_val) {
      _store.EraseAll();
      ps_unref_msg(msg);
      PS_PUB_BOOL(kTopicReset, true); // This restarts the esp
      continue;                       // we are done here, the esp will restart and reinitialize NVS

    } else if (ps_has_topic_suffix(msg, kTopicRegister)) {
      _store.RegisterSettings(parts.module.c_str(), (ConfigEntries *)msg->ptr_val);
      if (msg->rtopic != NULL) {
        PS_PUB_NIL(msg->rtopic);
      }
      ps_unref_msg(msg);
      continue;

    } else if (ps_has_topic_suffix(msg, TOPIC_DOT TOPIC_LIST)) {
      // TODO: listing namespaces is not implemented (ParseTopic never yields kVerbList; nothing was ever sent).
      ps_unref_msg(msg);
      continue;
    } else if (ps_has_topic_suffix(msg, TOPIC_DOT TOPIC_GET)) {
      if (!PS_IS_PTR(msg) && parts.module == kConfigSystemNamespace && parts.name.empty()) {
        ps_unref_msg(msg); // config.system.get is the application's (its description), not ours
        continue;
      }
      FLOG_DEBUG("verb: %i, target: %s, module: %s, name: %s", (int)parts.verb, parts.target.c_str(),
                 parts.module.c_str(), parts.name.c_str());
      if (PS_IS_PTR(msg)) {
        size_t count = _store.ReadSettings(parts.module.c_str(), *((std::shared_ptr<SettingsMap> *)msg->ptr_val));
        if (count == 0) {
          // TODO: no settings loaded?
          FLOG_ERROR("No settings found for %s", parts.module.c_str());
        }

        char topic[kMaxTopicLength];
        snprintf(topic, kMaxTopicLength, "%s.%s", parts.module.c_str(), kTopicConfigSet);
        FLOG_DEBUG("Loaded %d settings for %s", count, topic);
        if (msg->rtopic != NULL) {
          PS_PUB_NIL(msg->rtopic);
        } else {
          PS_PUB_NIL(topic);
        }
      } else if (parts.module != "") {
        if (parts.name != "") {
          SendPref(parts.module, parts.name);
        } else {
          SendPrefs(parts.module);
        }
      } else {
        SendPrefs();
      }
    } else if (ps_has_topic_suffix(msg, TOPIC_DOT TOPIC_SET)) {
      switch (msg->flags & PS_MSK_TYP) {
      case PS_INT_TYP:
        _store.WriteSetting(parts.module.c_str(), parts.name.c_str(), (int)msg->int_val);
        break;
      case PS_DBL_TYP:
        _store.WriteSetting(parts.module.c_str(), parts.name.c_str(), (double)msg->dbl_val);
        break;
      case PS_BOOL_TYP:
        _store.WriteSetting(parts.module.c_str(), parts.name.c_str(), (bool)msg->bool_val);
        break;
      case PS_STR_TYP:
        _store.WriteSetting(parts.module.c_str(), parts.name.c_str(), msg->str_val);
        break;
      default:
        FLOG_ERROR("Unknown type");
      }
      FLOG_DEBUG("Wrote %s", parts.name.c_str());
      char topic[kMaxTopicLength];
      parts.TopicGet(topic);
      FLOG_DEBUG("Informing %s about the config change", topic);
      PS_PUB_NIL(topic);
      SendPref(parts.module, parts.name);          // put changes on the protocol for a controller
    } else if (ps_has_topic_suffix(msg, ".add")) { // TODO: parametrize
      switch (msg->flags & PS_MSK_TYP) {
      case PS_STR_TYP: {
        FLOG_DEBUG("Adding %s to %s.%s", msg->str_val, parts.module.c_str(), parts.name.c_str());
        _store.AddToSetting(parts.module.c_str(), parts.name.c_str(), msg->str_val);
        char topic[kMaxTopicLength];
        parts.TopicGet(topic);
        FLOG_DEBUG("Informing %s about the config change", topic);
        PS_PUB_NIL(topic);
        break;
      }
      default:
        FLOG_ERROR("Unknown type, Can not Add");
      }
    } else if (ps_has_topic_suffix(msg, ".remove")) { // TODO: parametrize
      switch (msg->flags & PS_MSK_TYP) {
      case PS_STR_TYP: {
        FLOG_DEBUG("Removing %s from %s.%s", msg->str_val, parts.module.c_str(), parts.name.c_str());
        _store.RemoveFromSetting(parts.module.c_str(), parts.name.c_str(), msg->str_val);
        char topic[kMaxTopicLength];
        parts.TopicGet(topic);
        FLOG_DEBUG("Informing %s about the config change", topic);
        PS_PUB_NIL(topic);
        break;
      }
      default:
        FLOG_ERROR("Unknown type, Can not Remove");
      }
    } else if (ps_has_topic_suffix(msg, ".reset")) {
      _store.DeleteSetting(parts.module.c_str(), parts.name.c_str());
    } else {
      FLOG_DEBUG("Unhandled config topic %s", msg->topic);
    }
    ps_unref_msg(msg); // the get / set / add / remove / reset branches all end here
  }
}

void ConfigManager::SendPref(std::string module_name, std::string key) {
  ConfigEntry entry = _store.GetSetting(module_name, key);
  if (strlen(entry.key) == 0) {
    FLOG_ERROR("Key %s not found in module %s", key.c_str(), module_name.c_str());
    return;
  }
  ConfigValue value;
  if (!_store.ReadSetting(module_name.c_str(), entry.key, &value)) {
    return;
  }
  ConfigReport *msg = new ConfigReport();
  msg->module_name = module_name;
  msg->key = key;
  msg->description = entry.description;
  msg->format = entry.format;
  msg->unit = entry.unit;

  switch (entry.type) {
  case ConfigValueTypes::kInt:
    msg->value = std::to_string(std::get<int>(value));
    msg->default_value = std::to_string(std::get<int>(entry.default_value));
    break;
  case ConfigValueTypes::kDouble:
    msg->value = std::to_string(std::get<double>(value));
    msg->default_value = std::to_string(std::get<double>(entry.default_value));
    break;
  case ConfigValueTypes::kBool:
    msg->value = std::get<bool>(value) ? "True" : "False";
    msg->default_value = std::get<bool>(entry.default_value) ? "True" : "False";
    break;
  case ConfigValueTypes::kString:
    msg->value = std::get<std::string>(value);
    msg->default_value = std::get<std::string>(entry.default_value);
    break;
  case ConfigValueTypes::kVector:
    Serialize(std::get<ConfigVector>(value), msg->value);
    break;
  default:
    msg->value = "Not Impl";
  }
  FLOG_DEBUG("Sent config message for %s.%s: %s", module_name.c_str(), key.c_str(), msg->value.c_str());
  PS_PUB_BUF(kTopicReportConfig, msg, sizeof(ConfigReport), [](void *p) { delete static_cast<ConfigReport *>(p); });
}

void ConfigManager::SendPrefs(std::string module_name) {
  for (auto &entry : _store.GetEntries(module_name)) {
    SendPref(module_name, entry.key);
  }
}

void ConfigManager::SendPrefs() {
  for (auto &el : _store.Namespaces()) {
    SendPrefs(el);
  }
}

uint32_t ConfigManager::ParseTopic(const char *topic, TopicParts *parts) {
  char topic_copy[kMaxTopicLength];
  strncpy(topic_copy, topic, kMaxTopicLength - 1);
  topic_copy[kMaxTopicLength - 1] = '\0';
  const char *del = TOPIC_DOT;
  uint8_t counter = 0;
  bool verbed = false;

  char *saveptr = nullptr;
  char *t = strtok_r(topic_copy, del, &saveptr);
  if (t == nullptr) {
    return counter;
  }

  if (strcmp(t, TOPIC_INC_PREFIX) == 0) {
    parts->incoming = true;
    t = strtok_r(nullptr, del, &saveptr);
  }

  while (t != nullptr) {
    if (strcmp(t, kTopicVerbSet) == 0) {
      if (!verbed) {
        parts->verb = TopicVerbs::kVerbSet;
        verbed = true;
      }
      return counter;
    } else if (strcmp(t, kTopicVerbGet) == 0) {
      if (!verbed) {
        parts->verb = TopicVerbs::kVerbGet;
        verbed = true;
      }
    } else if (strcmp(t, kTopicVerbErase) == 0) {
      if (!verbed) {
        parts->verb = TopicVerbs::kVerbErase;
        verbed = true;
      }
    } else {
      switch (counter) {
      case 0:
        parts->target = t;
        counter++;
        break;
      case 1:
        parts->module = t;
        counter++;
        break;
      case 2:
        parts->name = t;
        counter++;
        break;
      default:
        break;
      }
    }
    t = strtok_r(nullptr, del, &saveptr);
  }
  return counter;
}

void RegisterConfig(ConfigEntries *config_entries, const char *name) {
  char t_topic[kMaxTopicLength];
  snprintf(t_topic, kMaxTopicLength, "%s.%s.%s", kTopicConfig, name, kTopicRegister);
  PS_CALL_PTR(t_topic, config_entries, 10000);
}

void GetSettings(std::shared_ptr<SettingsMap> &config, const char *name) {
  FLOG_DEBUG("Getting settings for %s", name);
  config->clear();
  char t_topic[kMaxTopicLength];
  char r_topic[32] = {0};
  ps_msg_t *ret_msg = NULL;

  snprintf(t_topic, kMaxTopicLength, "%s.%s.%s", kTopicConfig, name, TOPIC_GET);
  snprintf(r_topic, sizeof(r_topic), "$r.config.%u", ++rt_counter);
  ps_subscriber_t *su = ps_new_subscriber(1, PS_STRLIST(r_topic));

  ps_msg_t *config_msg = ps_new_msg(t_topic, PS_PTR_TYP, (void *)(&config));
  ps_msg_set_rtopic(config_msg, r_topic);
  ps_publish(config_msg);
  time_t timer = esp_timer_get_time() / 1000 + 10000; // wait for 10 seconds
  while (esp_timer_get_time() / 1000 < timer) {
    ret_msg = ps_get(su, 0);
    if (ret_msg != NULL) {
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(10)); // wait 10ms
  }

  if (ret_msg != NULL && PS_IS_NIL(ret_msg)) {
    FLOG_DEBUG("%s config received back", name);
  } else {
    FLOG_ERROR("Config FAILED for %s", name);
  }
  ps_unref_msg(ret_msg);
  ps_free_subscriber(su); // one reply subscriber per call
}

#endif // CONFIG_CONFIG_MANAGER_PUBSUB
