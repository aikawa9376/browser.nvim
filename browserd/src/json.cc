#include "json.h"

#include <charconv>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace browser {
namespace {

void AppendUtf8(std::string* output, std::uint32_t codepoint) {
  if (codepoint <= 0x7f) {
    output->push_back(static_cast<char>(codepoint));
  } else if (codepoint <= 0x7ff) {
    output->push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
    output->push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
  } else if (codepoint <= 0xffff) {
    output->push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
    output->push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
    output->push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
  } else {
    output->push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
    output->push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
    output->push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
    output->push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
  }
}

class Parser {
 public:
  explicit Parser(std::string_view input) : input_(input) {}

  std::optional<JsonValue> Parse(std::string* error) {
    try {
      SkipWhitespace();
      JsonValue result = ParseValue();
      SkipWhitespace();
      if (position_ != input_.size()) {
        Fail("trailing characters");
      }
      return result;
    } catch (const std::runtime_error& exception) {
      if (error) {
        *error = exception.what();
      }
      return std::nullopt;
    }
  }

 private:
  [[noreturn]] void Fail(std::string_view message) const {
    throw std::runtime_error("JSON parse error at byte " +
                             std::to_string(position_) + ": " +
                             std::string(message));
  }

  void SkipWhitespace() {
    while (position_ < input_.size()) {
      const char value = input_[position_];
      if (value != ' ' && value != '\t' && value != '\r' && value != '\n') {
        break;
      }
      ++position_;
    }
  }

  bool Consume(char expected) {
    if (position_ < input_.size() && input_[position_] == expected) {
      ++position_;
      return true;
    }
    return false;
  }

  void Expect(char expected) {
    if (!Consume(expected)) {
      Fail(std::string("expected '") + expected + "'");
    }
  }

  JsonValue ParseValue() {
    if (position_ >= input_.size()) {
      Fail("expected a value");
    }
    switch (input_[position_]) {
      case 'n':
        ParseLiteral("null");
        return JsonValue();
      case 't':
        ParseLiteral("true");
        return JsonValue(true);
      case 'f':
        ParseLiteral("false");
        return JsonValue(false);
      case '"':
        return JsonValue(ParseString());
      case '{':
        return JsonValue(ParseObject());
      case '[':
        return JsonValue(ParseArray());
      default:
        if (input_[position_] == '-' ||
            (input_[position_] >= '0' && input_[position_] <= '9')) {
          return JsonValue(ParseNumber());
        }
        Fail("unexpected character");
    }
  }

  void ParseLiteral(std::string_view literal) {
    if (input_.substr(position_, literal.size()) != literal) {
      Fail("invalid literal");
    }
    position_ += literal.size();
  }

  std::uint32_t ParseHex4() {
    if (position_ + 4 > input_.size()) {
      Fail("incomplete unicode escape");
    }
    std::uint32_t value = 0;
    for (int index = 0; index < 4; ++index) {
      const char digit = input_[position_++];
      value <<= 4;
      if (digit >= '0' && digit <= '9') {
        value += static_cast<std::uint32_t>(digit - '0');
      } else if (digit >= 'a' && digit <= 'f') {
        value += static_cast<std::uint32_t>(digit - 'a' + 10);
      } else if (digit >= 'A' && digit <= 'F') {
        value += static_cast<std::uint32_t>(digit - 'A' + 10);
      } else {
        Fail("invalid unicode escape");
      }
    }
    return value;
  }

  std::string ParseString() {
    Expect('"');
    std::string result;
    while (position_ < input_.size()) {
      const unsigned char byte = static_cast<unsigned char>(input_[position_++]);
      if (byte == '"') {
        return result;
      }
      if (byte < 0x20) {
        Fail("control character in string");
      }
      if (byte != '\\') {
        result.push_back(static_cast<char>(byte));
        continue;
      }
      if (position_ >= input_.size()) {
        Fail("incomplete escape");
      }
      const char escaped = input_[position_++];
      switch (escaped) {
        case '"': result.push_back('"'); break;
        case '\\': result.push_back('\\'); break;
        case '/': result.push_back('/'); break;
        case 'b': result.push_back('\b'); break;
        case 'f': result.push_back('\f'); break;
        case 'n': result.push_back('\n'); break;
        case 'r': result.push_back('\r'); break;
        case 't': result.push_back('\t'); break;
        case 'u': {
          std::uint32_t codepoint = ParseHex4();
          if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
            if (position_ + 2 > input_.size() || input_[position_] != '\\' ||
                input_[position_ + 1] != 'u') {
              Fail("missing low surrogate");
            }
            position_ += 2;
            const std::uint32_t low = ParseHex4();
            if (low < 0xdc00 || low > 0xdfff) {
              Fail("invalid low surrogate");
            }
            codepoint = 0x10000 + ((codepoint - 0xd800) << 10) +
                        (low - 0xdc00);
          } else if (codepoint >= 0xdc00 && codepoint <= 0xdfff) {
            Fail("unexpected low surrogate");
          }
          AppendUtf8(&result, codepoint);
          break;
        }
        default:
          Fail("invalid escape");
      }
    }
    Fail("unterminated string");
  }

  double ParseNumber() {
    const std::size_t start = position_;
    Consume('-');
    if (Consume('0')) {
      if (position_ < input_.size() && input_[position_] >= '0' &&
          input_[position_] <= '9') {
        Fail("leading zero in number");
      }
    } else {
      if (position_ >= input_.size() || input_[position_] < '1' ||
          input_[position_] > '9') {
        Fail("invalid number");
      }
      while (position_ < input_.size() && input_[position_] >= '0' &&
             input_[position_] <= '9') {
        ++position_;
      }
    }
    if (Consume('.')) {
      if (position_ >= input_.size() || input_[position_] < '0' ||
          input_[position_] > '9') {
        Fail("invalid fraction");
      }
      while (position_ < input_.size() && input_[position_] >= '0' &&
             input_[position_] <= '9') {
        ++position_;
      }
    }
    if (position_ < input_.size() &&
        (input_[position_] == 'e' || input_[position_] == 'E')) {
      ++position_;
      if (position_ < input_.size() &&
          (input_[position_] == '+' || input_[position_] == '-')) {
        ++position_;
      }
      if (position_ >= input_.size() || input_[position_] < '0' ||
          input_[position_] > '9') {
        Fail("invalid exponent");
      }
      while (position_ < input_.size() && input_[position_] >= '0' &&
             input_[position_] <= '9') {
        ++position_;
      }
    }
    const std::string token(input_.substr(start, position_ - start));
    char* end = nullptr;
    const double value = std::strtod(token.c_str(), &end);
    if (end != token.c_str() + token.size() || !std::isfinite(value)) {
      Fail("number is out of range");
    }
    return value;
  }

  JsonValue::Object ParseObject() {
    Expect('{');
    SkipWhitespace();
    JsonValue::Object result;
    if (Consume('}')) {
      return result;
    }
    while (true) {
      if (position_ >= input_.size() || input_[position_] != '"') {
        Fail("expected object key");
      }
      std::string key = ParseString();
      SkipWhitespace();
      Expect(':');
      SkipWhitespace();
      auto [_, inserted] = result.emplace(std::move(key), ParseValue());
      if (!inserted) {
        Fail("duplicate object key");
      }
      SkipWhitespace();
      if (Consume('}')) {
        return result;
      }
      Expect(',');
      SkipWhitespace();
    }
  }

  JsonValue::Array ParseArray() {
    Expect('[');
    SkipWhitespace();
    JsonValue::Array result;
    if (Consume(']')) {
      return result;
    }
    while (true) {
      result.push_back(ParseValue());
      SkipWhitespace();
      if (Consume(']')) {
        return result;
      }
      Expect(',');
      SkipWhitespace();
    }
  }

  std::string_view input_;
  std::size_t position_ = 0;
};

std::string EscapeString(std::string_view value) {
  std::ostringstream output;
  output << '"';
  for (const unsigned char byte : value) {
    switch (byte) {
      case '"': output << "\\\""; break;
      case '\\': output << "\\\\"; break;
      case '\b': output << "\\b"; break;
      case '\f': output << "\\f"; break;
      case '\n': output << "\\n"; break;
      case '\r': output << "\\r"; break;
      case '\t': output << "\\t"; break;
      default:
        if (byte < 0x20) {
          output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                 << static_cast<int>(byte) << std::dec;
        } else {
          output << static_cast<char>(byte);
        }
    }
  }
  output << '"';
  return output.str();
}

}  // namespace

JsonValue::JsonValue() = default;
JsonValue::JsonValue(std::nullptr_t) {}
JsonValue::JsonValue(bool value) : type_(Type::kBool), boolean_(value) {}
JsonValue::JsonValue(int value) : JsonValue(static_cast<std::int64_t>(value)) {}
JsonValue::JsonValue(std::int64_t value)
    : type_(Type::kNumber), number_(static_cast<double>(value)) {}
JsonValue::JsonValue(double value) : type_(Type::kNumber), number_(value) {}
JsonValue::JsonValue(std::string value)
    : type_(Type::kString), string_(std::move(value)) {}
JsonValue::JsonValue(const char* value) : JsonValue(std::string(value)) {}
JsonValue::JsonValue(Object value)
    : type_(Type::kObject), object_(std::move(value)) {}
JsonValue::JsonValue(Array value)
    : type_(Type::kArray), array_(std::move(value)) {}

const JsonValue* JsonValue::Find(std::string_view key) const {
  if (!is_object()) {
    return nullptr;
  }
  const auto found = object_.find(key);
  return found == object_.end() ? nullptr : &found->second;
}

std::optional<std::string> JsonValue::String(std::string_view key) const {
  const JsonValue* value = Find(key);
  if (!value || !value->is_string()) {
    return std::nullopt;
  }
  return value->string();
}

std::optional<std::int64_t> JsonValue::Integer(std::string_view key) const {
  const JsonValue* value = Find(key);
  if (!value || !value->is_number() || std::floor(value->number()) != value->number() ||
      value->number() < static_cast<double>(std::numeric_limits<std::int64_t>::min()) ||
      value->number() > static_cast<double>(std::numeric_limits<std::int64_t>::max())) {
    return std::nullopt;
  }
  return static_cast<std::int64_t>(value->number());
}

std::optional<bool> JsonValue::Boolean(std::string_view key) const {
  const JsonValue* value = Find(key);
  if (!value || !value->is_bool()) {
    return std::nullopt;
  }
  return value->boolean();
}

std::string JsonValue::Dump() const {
  switch (type_) {
    case Type::kNull:
      return "null";
    case Type::kBool:
      return boolean_ ? "true" : "false";
    case Type::kNumber: {
      std::ostringstream output;
      output << std::setprecision(17) << number_;
      return output.str();
    }
    case Type::kString:
      return EscapeString(string_);
    case Type::kObject: {
      std::string result = "{";
      bool first = true;
      for (const auto& [key, value] : object_) {
        if (!first) {
          result += ',';
        }
        first = false;
        result += EscapeString(key) + ":" + value.Dump();
      }
      return result + "}";
    }
    case Type::kArray: {
      std::string result = "[";
      bool first = true;
      for (const auto& value : array_) {
        if (!first) {
          result += ',';
        }
        first = false;
        result += value.Dump();
      }
      return result + "]";
    }
  }
  return "null";
}

std::optional<JsonValue> JsonValue::Parse(std::string_view input,
                                          std::string* error) {
  return Parser(input).Parse(error);
}

}  // namespace browser
