#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace browser {

class JsonValue {
 public:
  enum class Type { kNull, kBool, kNumber, kString, kObject, kArray };
  using Object = std::map<std::string, JsonValue, std::less<>>;
  using Array = std::vector<JsonValue>;

  JsonValue();
  JsonValue(std::nullptr_t);
  JsonValue(bool value);
  JsonValue(int value);
  JsonValue(std::int64_t value);
  JsonValue(double value);
  JsonValue(std::string value);
  JsonValue(const char* value);
  JsonValue(Object value);
  JsonValue(Array value);

  [[nodiscard]] Type type() const { return type_; }
  [[nodiscard]] bool is_object() const { return type_ == Type::kObject; }
  [[nodiscard]] bool is_string() const { return type_ == Type::kString; }
  [[nodiscard]] bool is_number() const { return type_ == Type::kNumber; }
  [[nodiscard]] bool is_bool() const { return type_ == Type::kBool; }

  [[nodiscard]] const Object& object() const { return object_; }
  [[nodiscard]] const Array& array() const { return array_; }
  [[nodiscard]] const std::string& string() const { return string_; }
  [[nodiscard]] double number() const { return number_; }
  [[nodiscard]] bool boolean() const { return boolean_; }

  [[nodiscard]] const JsonValue* Find(std::string_view key) const;
  [[nodiscard]] std::optional<std::string> String(std::string_view key) const;
  [[nodiscard]] std::optional<std::int64_t> Integer(std::string_view key) const;
  [[nodiscard]] std::optional<bool> Boolean(std::string_view key) const;

  [[nodiscard]] std::string Dump() const;
  static std::optional<JsonValue> Parse(std::string_view input,
                                        std::string* error = nullptr);

 private:
  Type type_ = Type::kNull;
  bool boolean_ = false;
  double number_ = 0;
  std::string string_;
  Object object_;
  Array array_;
};

}  // namespace browser
