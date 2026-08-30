#include "json.h"

#include <cassert>
#include <string>

int main() {
  std::string error;
  const auto parsed = browser::JsonValue::Parse(
      R"({"browser_id":7,"ok":true,"url":"https://例.example/a\n","emoji":"\ud83d\ude80"})",
      &error);
  assert(parsed.has_value());
  assert(parsed->is_object());
  assert(parsed->Integer("browser_id") == 7);
  assert(parsed->Boolean("ok") == true);
  assert(parsed->String("url") == "https://例.example/a\n");
  assert(parsed->String("emoji") == "🚀");

  const auto round_trip = browser::JsonValue::Parse(parsed->Dump(), &error);
  assert(round_trip.has_value());
  assert(round_trip->String("emoji") == "🚀");

  assert(!browser::JsonValue::Parse(R"({"a":1,"a":2})", &error));
  assert(!browser::JsonValue::Parse(R"({"broken":})", &error));
  return 0;
}
