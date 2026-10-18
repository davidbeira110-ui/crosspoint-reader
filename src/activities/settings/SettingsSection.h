#pragma once

#include <I18n.h>

#include <cstddef>
#include <cstdint>

// Top-level groups on the Settings screen, in display order. End doubles as
// "no section" where one is optional. Library and Network were folded into
// Reader and System; the aliases keep existing call sites routing correctly.
enum class SettingsSection : uint8_t {
  Reader,
  Display,
  Controls,
  General,
  End,
  Library = Reader,
  Network = General,
};

inline constexpr size_t SETTINGS_SECTION_COUNT = static_cast<size_t>(SettingsSection::End);

inline constexpr StrId SETTINGS_SECTION_TITLES[SETTINGS_SECTION_COUNT] = {
    StrId::STR_CAT_READER,
    StrId::STR_CAT_DISPLAY,
    StrId::STR_CAT_CONTROLS,
    StrId::STR_SETTINGS_GENERAL,
};
