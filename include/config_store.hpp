/**
 * @file config_store.hpp
 * @brief The configuration store: typed, described settings per namespace, kept in NVS.
 *
 * This is the core of the component and has no message bus in it: register a namespace's entries (defaults are
 * written for keys NVS does not have yet), then read and write values. Every public method is thread safe.
 *
 * The pubsub front-end (config.* topics, RegisterConfig / GetSettings) is ConfigManager in config_mgr.hpp, built
 * when CONFIG_CONFIG_MANAGER_PUBSUB is set.
 *
 * One store per program: all instances share the one NVS handle and lock.
 */
#pragma once

#include "funlog.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <variant>
#include <vector>

static constexpr char kConfigManagerNamespace[] = "_config_mngr";
static constexpr uint8_t kMaxNvsNameLength = 15;
static constexpr char kConfigSystemNamespace[] = "system"; //!< always registered by the store

enum ConfigValueTypes { kInt, kDouble, kBool, kString, kBytes, kVector };
inline const char *ConfigValueTypeToString(ConfigValueTypes type) {
  switch (type) {
  case ConfigValueTypes::kInt:
    return "kInt";
  case ConfigValueTypes::kDouble:
    return "kDouble";
  case ConfigValueTypes::kBool:
    return "kBool";
  case ConfigValueTypes::kString:
    return "kString";
  case ConfigValueTypes::kBytes:
    return "kBytes";
  case ConfigValueTypes::kVector:
    return "kVector";
  default:
    return "Unknown";
  }
}

using ConfigVector = std::vector<std::string>;

using ConfigValue = std::variant<int, double, bool, std::string, std::vector<uint8_t>, ConfigVector>;

/// @brief  Configuration entry structure.
/// This structure defines a configuration entry with a unique key, description, format, type, default value, and unit.
/// @struct ConfigEntry
/// @param key Unique key to identify the entry (max length defined by kMaxNvsNameLength).
/// @param description Description of the configuration entry.
/// @param format Format string that describes the expected value format.
/// @param type Type of the value (int, double, bool, string, bytes, vector).
/// @param default_value Default value for the configuration entry, stored as a ConfigValue variant.
/// @param unit Unit of the value, e.g., "RPM", "m/s", "A".
struct ConfigEntry {
  char key[kMaxNvsNameLength + 1] = {}; /*!< unique key to identify the entry, always NUL terminated */
  std::string description;   /*!< description */
  std::string format;        /*!< the format this msg is limited to*/
  ConfigValueTypes type;     /*!< type of the value */
  ConfigValue default_value; /*!< */
  std::string unit = "";     /*!< unit of the value, e.g. "RPM", "m/s", "A" */

  ConfigEntry(const char nkey[kMaxNvsNameLength], std::string descr, std::string iformat, int default_val,
              std::string iunit) {
    strncpy(key, nkey, kMaxNvsNameLength);
    description = descr;
    format = iformat;
    type = ConfigValueTypes::kInt;
    default_value = default_val;
    unit = iunit;
    if (!format.starts_with("int")) {
      FLOG_WARN("ConfigEntry %s has int type but format does not start with 'int'", key);
    }
  }
  ConfigEntry(const char nkey[kMaxNvsNameLength], std::string descr, std::string iformat, double default_val,
              std::string iunit) {
    strncpy(key, nkey, kMaxNvsNameLength);
    description = descr;
    format = iformat;
    type = ConfigValueTypes::kDouble;
    default_value = default_val;
    unit = iunit;
    if (!format.starts_with("double") && !format.starts_with("float")) {
      FLOG_WARN("ConfigEntry %s has double type but format does not start with 'double' or 'float'", key);
    }
  }
  ConfigEntry(const char nkey[kMaxNvsNameLength], std::string descr, std::string iformat, bool default_val,
              std::string iunit) {
    strncpy(key, nkey, kMaxNvsNameLength);
    description = descr;
    format = iformat;
    type = ConfigValueTypes::kBool;
    default_value = default_val;
    unit = iunit;
    if (!format.starts_with("bool")) {
      FLOG_WARN("ConfigEntry %s has bool type but format does not start with 'bool'", key);
    }
  }
  ConfigEntry(const char nkey[kMaxNvsNameLength], std::string descr, std::string iformat, std::string default_val,
              std::string iunit) {
    strncpy(key, nkey, kMaxNvsNameLength);
    description = descr;
    format = iformat;
    type = ConfigValueTypes::kString;
    default_value = default_val;
    unit = iunit;
    if (!format.starts_with("string")) {
      FLOG_WARN("ConfigEntry %s has string type but format does not start with 'string'", key);
    }
  }
  ConfigEntry(const char nkey[kMaxNvsNameLength], std::string descr, std::string iformat,
              const ConfigVector &default_val, std::string iunit) {
    strncpy(key, nkey, kMaxNvsNameLength);
    description = descr;
    format = iformat;
    type = ConfigValueTypes::kVector;
    default_value = default_val;
    unit = iunit;
    if (!format.starts_with("list")) {
      FLOG_WARN("ConfigEntry %s has list type but format does not start with 'list'", key);
    }
  }
  ConfigEntry() {
    key[0] = '\0'; // Initialize key to an empty string
    description = "";
    format = "";
    type = ConfigValueTypes::kString; // Default type
    default_value = "";               // Default value as an empty string
    unit = "";
  }
  // TODO: Bytes not implemented yet in config manager
};

/// @brief Validator for configuration values parsed from format string.
/// @link https://github.com/FunShoot/protocol/blob/develop/configformat.md
struct Validator {
  // range / len
  bool has_min = false, has_max = false;
  double min_val = 0.0, max_val = 0.0;
  bool has_len = false;
  size_t min_len = 0, max_len = 0;
  // list: len / enum apply to each item; count limits the number of items
  bool is_list = false;
  bool has_count = false;
  size_t min_count = 0, max_count = 0;
  // enum
  bool has_enum = false;
  std::vector<std::string> enum_vals; ///< string enum values
  std::vector<double> enum_num_vals;  ///< int / double enum values
  // ui hints only
  bool has_precision = false;
  int precision = 0;
  bool has_step = false;
  double step = 0.0;
  bool password = false;

  bool validate_int(int v) const {
    if (has_min && v < min_val)
      return false;
    if (has_max && v > max_val)
      return false;
    if (has_enum && !enum_num_vals.empty())
      return std::find(enum_num_vals.begin(), enum_num_vals.end(), static_cast<double>(v)) != enum_num_vals.end();
    return true;
  }

  bool validate_double(double v) const {
    if (has_min && v < min_val)
      return false;
    if (has_max && v > max_val)
      return false;
    if (has_enum && !enum_num_vals.empty())
      return std::find(enum_num_vals.begin(), enum_num_vals.end(), v) != enum_num_vals.end();
    return true;
  }

  bool validate_string(const std::string &v) const {
    if (has_len && (v.size() < min_len || v.size() > max_len))
      return false;
    if (has_enum && std::find(enum_vals.begin(), enum_vals.end(), v) == enum_vals.end())
      return false;
    return true;
  }

  bool validate_bool(bool) const { return true; }

  /// @brief One list item: the string rules, plus what the stored form needs (items are joined with '.', the bus
  /// delimiter, so an item can be neither empty nor contain one).
  bool validate_item(const std::string &v) const {
    if (v.empty() || v.find('.') != std::string::npos)
      return false;
    return validate_string(v);
  }

  /// @brief A whole list: every item, and the number of items.
  bool validate_list(const std::vector<std::string> &items) const {
    if (has_count && (items.size() < min_count || items.size() > max_count))
      return false;
    return std::all_of(items.begin(), items.end(), [this](const std::string &item) { return validate_item(item); });
  }
};

using ConfigEntries = std::vector<ConfigEntry>;

using ConfigMap = std::map<std::string, ConfigEntries>;
using SettingsMap = std::map<std::string, ConfigValue>;

class ConfigStore {
public:
  /// @brief Initialise NVS (formatting it when it is unusable) and load the namespace list. Call once, first.
  void Setup();
  size_t RegisterSettings(const char *name_space, ConfigEntries *config_entries);
  size_t ReadSettings(const char *name_space, std::shared_ptr<SettingsMap> settings);
  /// @brief One value: what NVS holds, or the entry's default when NVS does not have the key.
  /// @return false when the namespace or key is not registered.
  bool ReadSetting(const char *name_space, const char *key, ConfigValue *value);
  size_t DeleteSetting(const char *name_space, const char *name);
  size_t WriteSetting(const char *name_space, const char *name, int value);
  size_t WriteSetting(const char *name_space, const char *name, double value);
  size_t WriteSetting(const char *name_space, const char *name, bool value);
  size_t WriteSetting(const char *name_space, const char *name, const char *value);
  size_t WriteSetting(const char *name_space, const char *name, const std::string &value);
  size_t WriteSetting(const char *name_space, const char *name, const std::vector<std::string> &value);
  size_t AddToSetting(const char *name_space, const char *name, const std::string &value);
  size_t RemoveFromSetting(const char *name_space, const char *name, const std::string &value);
  ConfigEntry GetSetting(std::string name_space, std::string key);
  /// @brief The registered entries of a namespace (a copy; empty when unknown).
  ConfigEntries GetEntries(const std::string &name_space);
  /// @brief All namespaces known to NVS (a copy).
  std::vector<std::string> Namespaces();
  /// @brief Erase the whole NVS partition. The caller restarts the chip.
  void EraseAll();
  void ShowPrefs(std::string name_space);
  void ShowPrefs();

  static Validator ParseValidator(const std::string &format);

private:
  ConfigMap _entries;
  std::vector<std::string> _nvs_namespaces;

  bool InitializeNVS();
  bool AddNamespace(std::string name_space);
  bool RemoveNamespace(std::string name_space);
  bool ReadNamespaces();
  bool WriteNamespaces();
  ConfigValue ReadValue(const ConfigEntry &entry);
  /// @brief The rules of a registered entry's format (no rules when the entry is unknown).
  Validator ValidatorFor(const char *name_space, const char *name);
};

size_t Serialize(const std::vector<std::string> &vector, std::string &serialized);
size_t DeSerialize(const std::string *serialized, std::vector<std::string> &vector);
