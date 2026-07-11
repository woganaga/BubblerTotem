#include "WebCommon.h"

String colorToHex(CRGB c) {
  char buf[8];
  snprintf(buf, sizeof(buf), "#%02x%02x%02x", c.r, c.g, c.b);
  return String(buf);
}

CRGB hexToColor(const String& hex) {
  long value = strtol(hex.c_str() + 1, nullptr, 16);
  return CRGB((value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF);
}

String jsonEscape(const String& s) {
  String out;
  out.reserve(s.length() + 4);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') out += '\\';
    out += c;
  }
  return out;
}
