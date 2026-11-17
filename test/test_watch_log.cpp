#include "WatchLog.hpp"

#include <cassert>
#include <cstdio>

#if HACKTOR_LOG_LEVEL > WATCH_LOG_LEVEL_OFF
TestSerial Serial;
#endif

namespace {

void messagesRespectThresholdAndSkipDisabledArguments() {
  int evaluated = 0;
  WATCH_LOG_E("error=%d", ++evaluated);
  WATCH_LOG_W("warning=%d", ++evaluated);
  WATCH_LOG_I("info=%d", ++evaluated);
  WATCH_LOG_D("debug=%d", ++evaluated);
  WATCH_LOG_V("verbose=%d", ++evaluated);
  assert(evaluated == HACKTOR_LOG_LEVEL);

#if HACKTOR_LOG_LEVEL > WATCH_LOG_LEVEL_OFF
  std::string expected;
#if HACKTOR_LOG_LEVEL >= WATCH_LOG_LEVEL_ERROR
  expected += "[E] error=1\n";
#endif
#if HACKTOR_LOG_LEVEL >= WATCH_LOG_LEVEL_WARNING
  expected += "[W] warning=2\n";
#endif
#if HACKTOR_LOG_LEVEL >= WATCH_LOG_LEVEL_INFO
  expected += "[I] info=3\n";
#endif
#if HACKTOR_LOG_LEVEL >= WATCH_LOG_LEVEL_DEBUG
  expected += "[D] debug=4\n";
#endif
#if HACKTOR_LOG_LEVEL >= WATCH_LOG_LEVEL_VERBOSE
  expected += "[V] verbose=5\n";
#endif
  assert(Serial.output == expected);
  Serial.output.clear();
#endif
}

void macrosAreSafeInUnbracedControlFlow() {
  bool saw_else = false;
  if (false)
    WATCH_LOG_E("unreachable");
  else
    saw_else = true;
  assert(saw_else);

  int evaluated = 0;
  if (true)
    WATCH_LOG_V("branch=%d", ++evaluated);
  else
    assert(false);
  assert(evaluated == (HACKTOR_LOG_LEVEL == WATCH_LOG_LEVEL_VERBOSE ? 1 : 0));
#if HACKTOR_LOG_LEVEL > WATCH_LOG_LEVEL_OFF
  Serial.output.clear();
#endif
}

void literalOnlyMessagesNeedNoFormattingArguments() {
  WATCH_LOG_E("failure");
#if HACKTOR_LOG_LEVEL > WATCH_LOG_LEVEL_OFF
  assert(Serial.output == "[E] failure\n");
  Serial.output.clear();
#endif
}

// Disabled log operands need not exist at all, proving they do not leak into
// the compilation unit or require a Serial declaration when logging is off.
void disabledMessagesDoNotRequireSymbols() {
#if HACKTOR_LOG_LEVEL < WATCH_LOG_LEVEL_ERROR
  WATCH_LOG_E("stripped error %d", undefined_error_argument());
#endif
#if HACKTOR_LOG_LEVEL < WATCH_LOG_LEVEL_WARNING
  WATCH_LOG_W("stripped warning %d", undefined_warning_argument());
#endif
#if HACKTOR_LOG_LEVEL < WATCH_LOG_LEVEL_INFO
  WATCH_LOG_I("stripped info %d", undefined_info_argument());
#endif
#if HACKTOR_LOG_LEVEL < WATCH_LOG_LEVEL_DEBUG
  WATCH_LOG_D("stripped debug %d", undefined_debug_argument());
#endif
#if HACKTOR_LOG_LEVEL < WATCH_LOG_LEVEL_VERBOSE
  WATCH_LOG_V("stripped verbose %d", undefined_verbose_argument());
#endif
}

}  // namespace

int main() {
  messagesRespectThresholdAndSkipDisabledArguments();
  macrosAreSafeInUnbracedControlFlow();
  literalOnlyMessagesNeedNoFormattingArguments();
  disabledMessagesDoNotRequireSymbols();
  std::printf("Watch log level %d: all 4 scenarios passed.\n", HACKTOR_LOG_LEVEL);
}
