/**
 * @file config_store.cpp
 * @brief The configuration store: entries per namespace, values in NVS (through Preferences), format validators.
 */
#include "config_store.hpp"
#include "preferences.hpp"
#include <cctype>
#include <cstdlib>
#include <mutex>
#include <numeric>
#include <nvs_flash.h>
#include <tuple> // for std::get

#include <funlog.h>

namespace {
Preferences _prefs;            // one NVS handle, opened per operation
std::recursive_mutex s_mutex;  // guards _prefs and the store's maps; public methods call each other

// Placeholder entries for the "system" namespace (always registered).
ConfigEntries system_entries = {
    ConfigEntry("test", "Test entry", "bool", false, ""),
    ConfigEntry("test_vector", "Testing Vectors", "list:len=[1,15];count=[,8]", ConfigVector(), ""),
};
} // namespace

void ConfigStore::Setup() {
  std::lock_guard<std::recursive_mutex> lock(s_mutex);
  nvs_handle_t nvs_handle;
  esp_err_t err = nvs_open(kConfigManagerNamespace, NVS_READWRITE, &nvs_handle);
  nvs_close(nvs_handle);
  switch (err) {
  case ESP_ERR_NVS_NOT_INITIALIZED:
    FLOG_DEBUG("NVS not initialized, initializing now");
    // nvs_flash_init();
    InitializeNVS();
    break;
  case ESP_ERR_NVS_NOT_ENOUGH_SPACE:
    FLOG_ERROR("NVS not enough space, reformatting");
    InitializeNVS();
    break;
  case ESP_OK:
    FLOG_DEBUG("NVS opened successfully");
    break;
  default:
    FLOG_ERROR("NVS open failed with error: %s", esp_err_to_name(err));
  }

  if (!_prefs.begin(kConfigManagerNamespace)) {
    _prefs.end();
    FLOG_ERROR("WTF??? NVS wasn't initialized, doing it again...");
    InitializeNVS();
    return;
  };
  if (!_prefs.isKey("nvsInit")) {
    _prefs.end();
    FLOG_ERROR("nvsInit was not set");
    InitializeNVS();
    return;
  }
  _prefs.end();
  ReadNamespaces();
  std::string mod_list;
  RegisterSettings(kConfigSystemNamespace, &system_entries);
  for (auto i : _nvs_namespaces) {
    if (mod_list.length() > 0) {
      mod_list += ", ";
    }
    mod_list += i;
  }
  FLOG_INFO("Found modules %s", mod_list.c_str());
}

ConfigValue ConfigStore::ReadValue(const ConfigEntry &entry) {
  switch (entry.type) {
  case ConfigValueTypes::kInt:
    return static_cast<int>(_prefs.getInt(entry.key, std::get<int>(entry.default_value)));
  case ConfigValueTypes::kDouble:
    return _prefs.getDouble(entry.key, std::get<double>(entry.default_value));
  case ConfigValueTypes::kBool:
    return _prefs.getBool(entry.key, std::get<bool>(entry.default_value));
  case ConfigValueTypes::kString:
    return _prefs.getString(entry.key, std::get<std::string>(entry.default_value));
  case ConfigValueTypes::kVector: {
    if (!_prefs.isKey(entry.key)) {
      return entry.default_value;
    }
    std::string serial_data = _prefs.getString(entry.key, "");
    ConfigVector vec;
    DeSerialize(&serial_data, vec);
    return vec;
  }
  default:
    return entry.default_value;
  }
}

bool ConfigStore::ReadSetting(const char *name_space, const char *key, ConfigValue *value) {
  std::lock_guard<std::recursive_mutex> lock(s_mutex);
  ConfigEntry entry = GetSetting(name_space, key);
  if (entry.key[0] == '\0') {
    return false;
  }
  if (!_prefs.begin(name_space, true)) {
    _prefs.end();
    *value = entry.default_value; // namespace not in NVS yet: the default
    return true;
  }
  *value = ReadValue(entry);
  _prefs.end();
  return true;
}

ConfigEntries ConfigStore::GetEntries(const std::string &name_space) {
  std::lock_guard<std::recursive_mutex> lock(s_mutex);
  auto it = _entries.find(name_space);
  return it == _entries.end() ? ConfigEntries{} : it->second;
}

std::vector<std::string> ConfigStore::Namespaces() {
  std::lock_guard<std::recursive_mutex> lock(s_mutex);
  return _nvs_namespaces;
}

void ConfigStore::EraseAll() {
  std::lock_guard<std::recursive_mutex> lock(s_mutex);
  _prefs.end();
  FLOG_WARN("Erasing NVS Flash on request");
  ESP_ERROR_CHECK(nvs_flash_erase());
}

size_t ConfigStore::RegisterSettings(const char *name_space, ConfigEntries *config_entries) {
  std::lock_guard<std::recursive_mutex> lock(s_mutex);
  FLOG_DEBUG("Registering config entries for %s", name_space);
  size_t written = 0;
  std::string buf;
  AddNamespace(name_space);
  if (!_prefs.begin(name_space)) {
    _prefs.end();
    FLOG_DEBUG("Could not open namespace %s, Aborting...", name_space);
    return 0;
  };
  for (auto &element : *config_entries) {
    if (!_prefs.isKey(element.key)) { // TODO: atm, we don't check type, if it changes, manually format.
      FLOG_VERBOSE("New entry for %s of type %i", element.key, element.type);
      switch (element.type) {
      case ConfigValueTypes::kInt:
        written += _prefs.putInt(element.key, std::get<int>(element.default_value));
        break;
      case ConfigValueTypes::kDouble:
        written += _prefs.putDouble(element.key, std::get<double>(element.default_value));
        break;
      case ConfigValueTypes::kBool:
        written += _prefs.putBool(element.key, std::get<bool>(element.default_value));
        break;
      case ConfigValueTypes::kString:
        written += _prefs.putString(element.key, std::get<std::string>(element.default_value).c_str());
        break;
      // TODO: Figure out byte array
      case ConfigValueTypes::kBytes:
        FLOG_ERROR("Not implemented yet");
        //   _prefs.putBytes(element.key, std::get<std::vector<uint8_t>>(element.default_value),
        //                   strlen(std::get<std::vector<uint8_t>>(element.default_value)));
        break;
      case ConfigValueTypes::kVector:
        Serialize(std::get<std::vector<std::string>>(element.default_value), buf);
        FLOG_DEBUG("Register %s: %s", element.key, buf);
        written += _prefs.putString(element.key, buf);
        break;
      default:
        FLOG_ERROR("Unknown Type supplied.");
      }
    }

    _entries.insert_or_assign(name_space, *config_entries);
    // _entries[nspace] = *config_entries;
  };
  FLOG_DEBUG("Wrote %i bytes", written);
  _prefs.end();
  return written;
}

size_t ConfigStore::ReadSettings(const char *name_space, std::shared_ptr<SettingsMap> settings) {
  std::lock_guard<std::recursive_mutex> lock(s_mutex);
  uint8_t count = 0;
  _prefs.begin(name_space, true);
  for (auto &entry : _entries[name_space]) {
    if (!_prefs.isKey(entry.key)) {
      FLOG_ERROR("Setting does not exist in NVS: %s", entry.key);
      continue;
    }
    count++;
    switch (entry.type) {
    case ConfigValueTypes::kInt:
      settings->emplace(entry.key, _prefs.getInt(entry.key, std::get<int>(entry.default_value)));
      break;
    case ConfigValueTypes::kDouble:
      settings->emplace(entry.key,
                        static_cast<double>(_prefs.getDouble(entry.key, std::get<double>(entry.default_value))));
      break;
    case ConfigValueTypes::kBool:
      settings->emplace(entry.key, _prefs.getBool(entry.key, std::get<bool>(entry.default_value)));
      break;
    case ConfigValueTypes::kString:
      settings->emplace(
          entry.key,
          std::string(_prefs.getString(entry.key, std::get<std::string>(entry.default_value).c_str()).c_str()));
      break;
    case ConfigValueTypes::kBytes:
      FLOG_ERROR("Not implemented yet");
      count--;
      break;
    case ConfigValueTypes::kVector:
      FLOG_DEBUG("Reading vector for %s in namespace %s", entry.key, name_space);
      {
        std::string serial_data = _prefs.getString(entry.key, "");
        ConfigVector vec;
        DeSerialize(&serial_data, vec);
        settings->emplace(entry.key, vec);
      }
      break;
    default:
      FLOG_ERROR("Unknown type");
      count--;
    }
  }
  _prefs.end();
  // printf("Loaded %i settings for %s", count, name_space);

  return count;
}

size_t ConfigStore::DeleteSetting(const char *name_space, const char *name) {
  std::lock_guard<std::recursive_mutex> lock(s_mutex);
  _prefs.begin(name_space);
  _prefs.remove(name);
  _prefs.end();
  FLOG_INFO("Deleted `%s` in module `%s`", name, name_space);
  return 0; // TODO: check if delete was successful
}

Validator ConfigStore::ValidatorFor(const char *name_space, const char *name) {
  std::lock_guard<std::recursive_mutex> lock(s_mutex);
  const ConfigEntry entry = GetSetting(name_space, name);
  return entry.key[0] == '\0' ? Validator{} : ParseValidator(entry.format);
}

size_t ConfigStore::WriteSetting(const char *name_space, const char *name, int value) {
  std::lock_guard<std::recursive_mutex> lock(s_mutex);
  if (!ValidatorFor(name_space, name).validate_int(value)) {
    FLOG_ERROR("%d not allowed for %s.%s, not written", value, name_space, name);
    return 0;
  }
  _prefs.begin(name_space);
  size_t size = _prefs.putInt(name, value);
  _prefs.end();
  return size;
}

size_t ConfigStore::WriteSetting(const char *name_space, const char *name, double value) {
  std::lock_guard<std::recursive_mutex> lock(s_mutex);
  if (!ValidatorFor(name_space, name).validate_double(value)) {
    FLOG_ERROR("%f not allowed for %s.%s, not written", value, name_space, name);
    return 0;
  }
  _prefs.begin(name_space);
  size_t size = _prefs.putDouble(name, value);
  _prefs.end();
  return size;
}

size_t ConfigStore::WriteSetting(const char *name_space, const char *name, bool value) {
  std::lock_guard<std::recursive_mutex> lock(s_mutex);
  _prefs.begin(name_space);
  size_t size = _prefs.putBool(name, value);
  bool test = _prefs.getBool(name);
  if (test != value) {
    FLOG_DEBUG("Failed to set bool %s", name);
  }
  _prefs.end();
  return size;
}

size_t ConfigStore::WriteSetting(const char *name_space, const char *name, const char *value) {
  return WriteSetting(name_space, name, std::string(value));
}

size_t ConfigStore::WriteSetting(const char *name_space, const char *name, const std::string &value) {
  std::lock_guard<std::recursive_mutex> lock(s_mutex);
  const std::optional<std::string> normalized = ValidatorFor(name_space, name).normalize_string(value);
  if (!normalized) {
    FLOG_ERROR("'%s' not allowed for %s.%s, not written", value.c_str(), name_space, name);
    return 0;
  }
  _prefs.begin(name_space);
  size_t size = _prefs.putString(name, *normalized);
  _prefs.end();
  return size;
}

size_t ConfigStore::WriteSetting(const char *name_space, const char *name, const ConfigVector &value) {
  std::lock_guard<std::recursive_mutex> lock(s_mutex);
  const std::optional<ConfigVector> normalized = ValidatorFor(name_space, name).normalize_list(value);
  if (!normalized) {
    FLOG_ERROR("List not allowed for %s.%s, not written", name_space, name);
    return 0;
  }
  std::string serial_data;
  Serialize(*normalized, serial_data);
  _prefs.begin(name_space);
  size_t size = _prefs.putString(name, serial_data);
  _prefs.end();
  return size;
}

size_t ConfigStore::AddToSetting(const char *name_space, const char *name, const std::string &value) {
  std::lock_guard<std::recursive_mutex> lock(s_mutex);
  const ConfigEntries entry = _entries[name_space];
  auto it = std::find_if(entry.begin(), entry.end(), [name](const ConfigEntry &e) { return strcmp(e.key, name) == 0; });
  if (it == entry.end()) {
    FLOG_DEBUG("Not Found %s.%s", name_space, name);
    return 0;
  }
  if (!std::holds_alternative<ConfigVector>(it->default_value)) {
    FLOG_DEBUG("Wrong Type: %s", ConfigValueTypeToString(it->type));
    return 0;
  }
  const Validator rules = ParseValidator(it->format);
  const std::optional<std::string> item = rules.normalize_item(value);
  if (!item) {
    FLOG_ERROR("'%s' not allowed in %s.%s, not added", value.c_str(), name_space, name);
    return 0;
  }
  _prefs.begin(name_space);
  std::vector<std::string> vec;
  std::string current_value = _prefs.getString(name, "");
  DeSerialize(&current_value, vec);
  if (std::find(vec.begin(), vec.end(), *item) != vec.end()) {
    FLOG_ERROR("Value %s already exists in %s.%s, not adding again", value.c_str(), name_space, name);
    _prefs.end();
    return 0; // nothing changed, so return 0
  }
  if (rules.has_count && vec.size() >= rules.max_count) {
    FLOG_ERROR("%s.%s is full (%u items), %s not added", name_space, name, (unsigned)vec.size(), value.c_str());
    _prefs.end();
    return 0;
  }
  vec.push_back(*item);
  Serialize(vec, current_value);
  size_t size = _prefs.putString(name, current_value);
  _prefs.end();
  return size;
}

size_t ConfigStore::RemoveFromSetting(const char *name_space, const char *name, const std::string &value) {
  std::lock_guard<std::recursive_mutex> lock(s_mutex);
  const ConfigEntries entry = _entries[name_space];
  auto it = std::find_if(entry.begin(), entry.end(), [name](const ConfigEntry &e) { return strcmp(e.key, name) == 0; });
  if (it == entry.end()) {
    FLOG_DEBUG("Not Found %s.%s", name_space, name);
    return 0;
  }
  if (!std::holds_alternative<ConfigVector>(it->default_value)) {
    FLOG_DEBUG("Wrong Type: %s", ConfigValueTypeToString(it->type));
    return 0;
  }
  _prefs.begin(name_space);
  std::vector<std::string> vec;
  std::string current_value = _prefs.getString(name, "");
  DeSerialize(&current_value, vec);
  const Validator rules = ParseValidator(it->format);
  const std::string item = rules.normalize_item(value).value_or(value); // "Fast Heat" removes "fast_heat"
  auto found = std::find(vec.begin(), vec.end(), item);
  if (found == vec.end()) {
    FLOG_ERROR("Value %s not in %s.%s, nothing to remove", value.c_str(), name_space, name);
    _prefs.end();
    return 0; // nothing changed, so return 0
  }
  if (rules.has_count && vec.size() <= rules.min_count) {
    FLOG_ERROR("%s.%s needs at least %u items, %s not removed", name_space, name, (unsigned)rules.min_count,
               value.c_str());
    _prefs.end();
    return 0;
  }
  vec.erase(found); // AddToSetting keeps entries unique, so there is at most one
  Serialize(vec, current_value);
  size_t size = _prefs.putString(name, current_value);
  _prefs.end();
  return size;
}

ConfigEntry ConfigStore::GetSetting(std::string name_space, std::string key) {
  std::lock_guard<std::recursive_mutex> lock(s_mutex);
  if (!_entries.contains(name_space.c_str())) {
    FLOG_VERBOSE("Module %s not found", name_space.c_str());
    return ConfigEntry{};
  }
  if (_entries[name_space.c_str()].empty()) {
    FLOG_VERBOSE("No entries for module %s", name_space.c_str());
    return ConfigEntry{};
  }
  auto it = _entries.find(name_space.c_str());
  for (ConfigEntry &entry : it->second) {
    if (entry.key == key)
      return entry;
  }
  FLOG_VERBOSE("Key %s not found in %s", key.c_str(), name_space.c_str());
  return ConfigEntry{};
}

void ConfigStore::ShowPrefs() {
  std::lock_guard<std::recursive_mutex> lock(s_mutex);
  for (auto &el : _nvs_namespaces) {
    ShowPrefs(el);
  }
}

void ConfigStore::ShowPrefs(std::string name_space) {
  std::lock_guard<std::recursive_mutex> lock(s_mutex);
  char cfg_line[180];
  if (!_prefs.begin(name_space.c_str(), true)) {
    FLOG_ERROR("No NVS space %s", name_space.c_str());
    _prefs.end();
    return;
  }
  std::string config_dump;
  // snprintf(cfg_line, 180, "%-15s %10s  %s", "Key", "Value", "Description");

  for (auto &entry : _entries[name_space.c_str()]) {
    std::string value;
    switch (entry.type) {
    case ConfigValueTypes::kInt:
      if (_prefs.isKey(entry.key)) {
        value = std::to_string(_prefs.getInt(entry.key, std::get<int>(entry.default_value)));
      } else {
        value = std::to_string(std::get<int>(entry.default_value));
      }
      break;
    case ConfigValueTypes::kDouble:
      if (_prefs.isKey(entry.key)) {
        value = std::to_string(_prefs.getInt(entry.key, std::get<double>(entry.default_value)));
      } else {
        value = std::to_string(std::get<double>(entry.default_value));
      }
      break;
    case ConfigValueTypes::kBool:
      if (_prefs.getBool(entry.key, std::get<bool>(entry.default_value))) {
        value = "True";
      } else {
        value = "False";
      }
      break;
    case ConfigValueTypes::kString:
      value = _prefs.getString(entry.key, std::get<std::string>(entry.default_value).c_str()).c_str();
      break;
    default:
      value = "Not Impl";
    }
    snprintf(cfg_line, 180, "%-15s %10s  %-s", entry.key, value.c_str(), entry.description.c_str());
    FLOG_INFO("%s", cfg_line);
    // PS_PUB_STR(kTopicReport, cfg_line);
  }
  // _prefs.end();
}

bool ConfigStore::InitializeNVS() {
  FLOG_INFO("Initializing Non-Volatile Storage");
  _prefs.end();
  // FLOG_DEBUG("Erasing NVS Flash");
  // ESP_ERROR_CHECK(nvs_flash_erase()); // TODO: do we need to erase?
  FLOG_DEBUG("Initializing NVS Flash");
  ESP_ERROR_CHECK(nvs_flash_init());
  FLOG_DEBUG("Opening NVS Namespace %s", kConfigManagerNamespace);
  _prefs.begin(kConfigManagerNamespace);
  _prefs.putBool("nvsInit", true);
  _prefs.end();
  // FLOG_DEBUG("Adding NVS Namespace", kConfigManagerNamespace);
  // AddNamespace(kConfigManagerNamespace);
  // FLOG_DEBUG("RESETTING ESP32");
  // PS_PUB_BOOL(kTopicReset, true);
  return false;
}

bool ConfigStore::AddNamespace(std::string name_space) {
  if (std::find(_nvs_namespaces.begin(), _nvs_namespaces.end(), name_space) != _nvs_namespaces.end()) {
    return true;
  }
  _nvs_namespaces.push_back(name_space);
  std::sort(_nvs_namespaces.begin(), _nvs_namespaces.end());
  return WriteNamespaces();
}

bool ConfigStore::RemoveNamespace(std::string name_space) {
  auto it = std::find(_nvs_namespaces.begin(), _nvs_namespaces.end(), name_space);
  if (it == _nvs_namespaces.end()) {
    return true;
  }

  _prefs.begin(name_space.c_str());
  _prefs.clear(); // remove all keys in this namespace
  _prefs.end();
  _nvs_namespaces.erase(it);

  return WriteNamespaces();
}

bool ConfigStore::ReadNamespaces() {
  FLOG_DEBUG("Reading NVS Namespaces");
  _prefs.end();
  if (!_prefs.begin(kConfigManagerNamespace, true)) {
    FLOG_DEBUG("Can not open namespace %s", kConfigManagerNamespace);
    _prefs.end();
    return false;
  }
  char dump[512] = {};

  if (!_prefs.isKey("namespaces")) {
    FLOG_DEBUG("No namespaces found");
    _prefs.end();
    WriteNamespaces();
    return false;
  }

  size_t arr_size = _prefs.getBytesLength("namespaces");
  _prefs.getBytes("namespaces", dump, arr_size);
  const char *del = ".";
  char *t = strtok(dump, del);
  while (t != nullptr) {
    _nvs_namespaces.push_back(t);
    t = strtok(nullptr, del);
  }
  _prefs.end();
  return false;
}

bool ConfigStore::WriteNamespaces() {
  if (!_prefs.begin(kConfigManagerNamespace)) {
    FLOG_DEBUG("Can not open namespace %s", kConfigManagerNamespace);
    _prefs.end();
    return false;
  }

  std::string buff =
      std::accumulate(_nvs_namespaces.begin(), _nvs_namespaces.end(), std::string(),
                      [](const std::string &ss, const std::string &s) { return ss.empty() ? s : ss + "." + s; });

  FLOG_DEBUG("Writing namespaces %s", buff.c_str());
  _prefs.putBytes("namespaces", buff.c_str(), buff.length() + 1);
  _prefs.end();
  return true;
}
namespace {

std::string TrimCopy(const std::string &s) {
  size_t b = 0, e = s.size();
  while (b < e && std::isspace(static_cast<unsigned char>(s[b])))
    ++b;
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])))
    --e;
  return s.substr(b, e - b);
}

bool ParseDoubleStrict(const std::string &s, double *out) {
  char *end = nullptr;
  const double v = std::strtod(s.c_str(), &end);
  if (end == s.c_str() || *end != '\0')
    return false;
  *out = v;
  return true;
}

bool ParseIntStrict(const std::string &s, int *out) {
  char *end = nullptr;
  const long v = std::strtol(s.c_str(), &end, 10);
  if (end == s.c_str() || *end != '\0')
    return false;
  *out = static_cast<int>(v);
  return true;
}

/// @brief Split a string on ';', returning trimmed non-empty tokens.
std::vector<std::string> SplitSemi(const std::string &s) {
  std::vector<std::string> out;
  size_t pos = 0;
  while (true) {
    const size_t semi = s.find(';', pos);
    auto tok = TrimCopy(s.substr(pos, semi == std::string::npos ? std::string::npos : semi - pos));
    if (!tok.empty())
      out.push_back(tok);
    if (semi == std::string::npos)
      break;
    pos = semi + 1;
  }
  return out;
}

/// @brief Split "key=val" on first '='. Returns false in ok if no '=' found.
struct RuleParts {
  std::string key, val;
  bool ok;
};
RuleParts SplitRule(const std::string &rule) {
  const size_t eq = rule.find('=');
  if (eq == std::string::npos)
    return {{}, {}, false};
  return {TrimCopy(rule.substr(0, eq)), TrimCopy(rule.substr(eq + 1)), true};
}

/// @brief Parse "[lo,hi]" range bracket. Either bound may be empty (open-ended).
bool ParseBracketRange(const std::string &raw, bool *has_lo, double *lo, bool *has_hi, double *hi) {
  const std::string s = TrimCopy(raw);
  if (s.size() < 3 || s.front() != '[' || s.back() != ']')
    return false;
  const std::string body = s.substr(1, s.size() - 2);
  const size_t comma = body.find(',');
  if (comma == std::string::npos || body.find(',', comma + 1) != std::string::npos)
    return false;
  const std::string left = TrimCopy(body.substr(0, comma));
  const std::string right = TrimCopy(body.substr(comma + 1));
  *has_lo = !left.empty();
  *has_hi = !right.empty();
  if (!*has_lo && !*has_hi)
    return false;
  if (*has_lo && !ParseDoubleStrict(left, lo))
    return false;
  if (*has_hi && !ParseDoubleStrict(right, hi))
    return false;
  if (*has_lo && *has_hi && *lo > *hi)
    return false;
  return true;
}

/// @brief Parse "[1,2,3]" or "[1.5,2.0]". Set ints_only for int enum.
bool ParseEnumNumberList(const std::string &raw, bool ints_only, std::vector<double> *out) {
  out->clear();
  const std::string s = TrimCopy(raw);
  if (s.size() < 2 || s.front() != '[' || s.back() != ']')
    return false;
  const std::string body = s.substr(1, s.size() - 2);
  size_t pos = 0;
  while (true) {
    const size_t comma = body.find(',', pos);
    const auto tok = TrimCopy(body.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos));
    if (tok.empty())
      return false;
    if (ints_only) {
      int iv = 0;
      if (!ParseIntStrict(tok, &iv))
        return false;
      out->push_back(static_cast<double>(iv));
    } else {
      double dv = 0.0;
      if (!ParseDoubleStrict(tok, &dv))
        return false;
      out->push_back(dv);
    }
    if (comma == std::string::npos)
      break;
    pos = comma + 1;
  }
  return !out->empty();
}

bool UnescapeQuoted(const std::string &in, std::string *out) {
  out->clear();
  for (size_t i = 0; i < in.size(); ++i) {
    if (in[i] != '\\') {
      out->push_back(in[i]);
      continue;
    }
    if (i + 1 >= in.size())
      return false;
    switch (in[++i]) {
    case '\\':
      out->push_back('\\');
      break;
    case '"':
      out->push_back('"');
      break;
    case 'n':
      out->push_back('\n');
      break;
    case 'r':
      out->push_back('\r');
      break;
    case 't':
      out->push_back('\t');
      break;
    default:
      return false;
    }
  }
  return true;
}

/// @brief Parse `["a","b,c"]` — quote/escape-aware string enum list.
bool ParseEnumStringList(const std::string &raw, std::vector<std::string> *out) {
  out->clear();
  const std::string s = TrimCopy(raw);
  if (s.size() < 2 || s.front() != '[' || s.back() != ']')
    return false;
  const std::string body = s.substr(1, s.size() - 2);
  size_t i = 0;
  while (i < body.size()) {
    while (i < body.size() && std::isspace(static_cast<unsigned char>(body[i])))
      ++i;
    if (i >= body.size())
      break;
    if (body[i] != '"')
      return false;
    ++i;
    std::string raw_tok;
    bool esc = false;
    while (i < body.size()) {
      const char c = body[i++];
      if (esc) {
        raw_tok.push_back('\\');
        raw_tok.push_back(c);
        esc = false;
        continue;
      }
      if (c == '\\') {
        esc = true;
        continue;
      }
      if (c == '"')
        break;
      raw_tok.push_back(c);
    }
    if (esc || body[i - 1] != '"')
      return false;
    std::string decoded;
    if (!UnescapeQuoted(raw_tok, &decoded))
      return false;
    out->push_back(decoded);
    while (i < body.size() && std::isspace(static_cast<unsigned char>(body[i])))
      ++i;
    if (i >= body.size())
      break;
    if (body[i] != ',')
      return false;
    ++i;
  }
  return !out->empty();
}

} // namespace

Validator ConfigStore::ParseValidator(const std::string &format) {
  Validator v{};
  const std::string s = TrimCopy(format);
  if (s.empty())
    return v;

  // Grammar: TYPE | TYPE ":" RULE (";" RULE)*  — split on first ':' only
  const size_t colon = s.find(':');
  const std::string type = TrimCopy(s.substr(0, colon));
  const std::string rules = (colon == std::string::npos) ? "" : s.substr(colon + 1);

  const bool is_int = type == "int";
  const bool is_double = type == "double";
  const bool is_string = type == "string";
  const bool is_bool = type == "bool";
  const bool is_nil = type == "nil";
  const bool is_list = type == "list"; // a list of strings: string rules apply per item
  if (!(is_int || is_double || is_string || is_bool || is_nil || is_list))
    return {};
  v.is_list = is_list;
  const bool is_text = is_string || is_list;

  if (rules.empty() || is_bool || is_nil)
    return v;

  for (const auto &rule : SplitSemi(rules)) {
    // flag form: bare "password" (no '=')
    if (rule == "password") {
      v.password = is_string;
      continue;
    }

    auto [key, val, ok] = SplitRule(rule);
    if (!ok)
      continue; // unknown/malformed — skip per spec

    if ((is_int || is_double) && key == "range") {
      bool has_lo = false, has_hi = false;
      double lo = 0.0, hi = 0.0;
      if (ParseBracketRange(val, &has_lo, &lo, &has_hi, &hi)) {
        v.has_min = has_lo;
        if (has_lo)
          v.min_val = lo;
        v.has_max = has_hi;
        if (has_hi)
          v.max_val = hi;
      }

    } else if (is_text && key == "len") {
      bool has_lo = false, has_hi = false;
      double lo = 0.0, hi = 0.0;
      if (ParseBracketRange(val, &has_lo, &lo, &has_hi, &hi)) {
        v.has_len = true;
        v.min_len = has_lo ? static_cast<size_t>(lo) : 0; // [,N] => [0,N]
        v.max_len = has_hi ? static_cast<size_t>(hi) : SIZE_MAX; // [N,] => no upper bound
      }

    } else if (is_list && key == "count") {
      bool has_lo = false, has_hi = false;
      double lo = 0.0, hi = 0.0;
      if (ParseBracketRange(val, &has_lo, &lo, &has_hi, &hi)) {
        v.has_count = true;
        v.min_count = has_lo ? static_cast<size_t>(lo) : 0;
        v.max_count = has_hi ? static_cast<size_t>(hi) : SIZE_MAX;
      }

    } else if ((is_int || is_double) && key == "enum") {
      std::vector<double> tmp;
      if (ParseEnumNumberList(val, is_int, &tmp)) {
        v.has_enum = true;
        v.enum_num_vals = std::move(tmp);
      }

    } else if (is_text && key == "enum") {
      std::vector<std::string> tmp;
      if (ParseEnumStringList(val, &tmp)) {
        v.has_enum = true;
        v.enum_vals = std::move(tmp);
      }

    } else if (is_double && key == "precision") {
      int p = 0;
      if (ParseIntStrict(val, &p) && p >= 0) {
        v.has_precision = true;
        v.precision = p;
      }

    } else if ((is_int || is_double) && key == "step") {
      double step = 0.0;
      if (ParseDoubleStrict(val, &step)) {
        v.has_step = true;
        v.step = step;
      }

    } else if (is_string && key == "password") {
      if (val == "true")
        v.password = true;
      else if (val == "false")
        v.password = false;
    }
    // any other key: silently skip per spec
  }
  return v;
}

size_t Serialize(const std::vector<std::string> &vector, std::string &serialized) {
  serialized =
      std::accumulate(vector.begin(), vector.end(), std::string(),
                      [](const std::string &ss, const std::string &s) { return ss.empty() ? s : ss + "." + s; });
  return serialized.length();
}

size_t DeSerialize(const std::string *serialized, std::vector<std::string> &vector) {
  vector.clear();
  const char *del = ".";
  char *t = strtok((char *)serialized->c_str(), del);
  while (t != nullptr) {
    vector.push_back(t);
    t = strtok(nullptr, del);
  }
  return vector.size();
}
