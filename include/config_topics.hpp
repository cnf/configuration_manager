/**
 * @file config_topics.hpp
 * @brief Bus topics the configuration manager listens on and publishes, and the shared topic grammar it parses.
 *
 * A project's own topic header includes this one and must not define these names again.
 */
#pragma once

static constexpr const int kMaxTopicLength = 64;

#define TOPIC_INC_PREFIX "inc"
#define TOPIC_DOT "." // Do NOT change this, the pubsub-c code expects a .
#define TOPIC_SET "set"
#define TOPIC_GET "get"
#define TOPIC_ERASE "erase"
#define TOPIC_LIST "list"

static constexpr const char kTopicDelimiter[] = ".";
static constexpr const char kTopicVerbSet[] = "set";
static constexpr const char kTopicVerbGet[] = "get";
static constexpr const char kTopicVerbErase[] = "erase";

//   config.<ns>.register        PTR ConfigEntries*, rtopic answered with NIL once registered (RegisterConfig)
//   config.<ns>.get             PTR shared_ptr<SettingsMap>*: filled in, rtopic answered with NIL (GetSettings)
//   [inc.]config[.<ns>[.<key>]].get   no PTR: report the values
//   [inc.]config.<ns>.<key>.set|add|remove|reset   change a value; <ns>.config.get is published after a change
//   inc.config.format           BOOL true: erase NVS and reset
//   [inc.]config.system.get     not answered here: the application's (e.g. a firmware description)
static constexpr const char kTopicConfig[] = "config";
static constexpr const char kTopicConfigGet[] = "config.get";
static constexpr const char kTopicConfigSet[] = "config.set";
static constexpr const char kTopicIncConfig[] = "inc.config";
static constexpr const char kTopicRegister[] = "register";
//   config.<ns>.entries         with an rtopic: the reply is a BUF holding a copy of the namespace's ConfigEntries,
//                               or NIL when it is not registered (what GetConfigEntries sends). No verb, so it
//                               cannot clash with a key.
static constexpr const char kTopicEntries[] = "entries";
static constexpr const char kTopicConfigFormat[] = "inc.config.format";
static constexpr const char kTopicConfigSystemGet[] = "config.system.get";
static constexpr const char kTopicIncConfigSystemGet[] = "inc.config.system.get";
static constexpr const char kTopicReset[] = "reset"; //!< published to restart the chip (after a format)

//   report.config               BUF ConfigReport: one value, published by SendPref (after a change, or on a get)
static constexpr const char kTopicReportConfig[] = "report.config";

// Shared bus names (not config's own): outgoing traffic for a transport, and the report/event/metric streams.
static constexpr const char kTopicTo[] = "to"; //!< to.<destination>.<topic>
static constexpr const char kTopicErrors[] = "errors";
static constexpr const char kTopicReport[] = "report";
static constexpr const char kTopicEvents[] = "events";
static constexpr const char kTopicMetrics[] = "metrics";
