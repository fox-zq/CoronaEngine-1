#pragma once

#include <include/internal/cef_types.h>

namespace Corona::Systems::UI::KeyUtils {

int convert_sdl_key_code_to_windows(int sdl_key);
bool is_modifier_key(int key);
bool should_send_char_event(int key, int modifiers);

}  // namespace Corona::Systems::UI::KeyUtils
