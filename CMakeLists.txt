# configuration_manager: typed settings in NVS (ConfigStore), optionally served on the pubsub-c bus (ConfigManager).
# The pubsub front-end is chosen by CONFIG_CONFIG_MANAGER_PUBSUB (Kconfig). Requirements are unconditional on
# purpose: with MINIMAL_BUILD the component graph is fixed before sdkconfig exists. The front-end source guards
# itself, so with the option off pubsub-c is fetched but nothing of it is linked in.
idf_component_register(
  SRCS
    "src/config_store.cpp"
    "src/preferences.cpp"
    "src/config_mgr.cpp"
  INCLUDE_DIRS "include"
  PRIV_INCLUDE_DIRS "src"
  REQUIRES
    FunLog
    pubsub-c
  PRIV_REQUIRES
    nvs_flash
    esp_timer
)
