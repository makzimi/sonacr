#include "manifest.hpp"

#include "audio_decoder.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace local_acr::cli {

namespace {

constexpr std::uintmax_t kMaxManifestBytes = 1024ULL * 1024ULL;
constexpr std::uintmax_t kMaxAudioBytes = 100ULL * 1024ULL * 1024ULL;
constexpr std::size_t kMaxMetadataBytes = 16U * 1024U;
constexpr std::size_t kMaxDisplayNameBytes = 256U;
constexpr std::size_t kMaxTriggers = 100U;

struct Json;
using Object = std::vector<std::pair<std::string, Json>>;
using Array = std::vector<Json>;

struct Json final {
  using Value = std::variant<std::nullptr_t, std::string, std::int64_t, Object, Array>;
  Value value = nullptr;
};

bool valid_utf8(std::string_view text) {
  std::size_t i = 0;
  while (i < text.size()) {
    const auto lead = static_cast<unsigned char>(text[i]);
    if (lead <= 0x7FU) {
      ++i;
      continue;
    }

    std::uint32_t code_point = 0;
    std::size_t continuation_count = 0;
    if (lead >= 0xC2U && lead <= 0xDFU) {
      code_point = lead & 0x1FU;
      continuation_count = 1;
    } else if (lead >= 0xE0U && lead <= 0xEFU) {
      code_point = lead & 0x0FU;
      continuation_count = 2;
    } else if (lead >= 0xF0U && lead <= 0xF4U) {
      code_point = lead & 0x07U;
      continuation_count = 3;
    } else {
      return false;
    }

    if (i + continuation_count >= text.size()) {
      return false;
    }
    for (std::size_t j = 1; j <= continuation_count; ++j) {
      const auto byte = static_cast<unsigned char>(text[i + j]);
      if ((byte & 0xC0U) != 0x80U) {
        return false;
      }
      code_point = (code_point << 6U) | (byte & 0x3FU);
    }

    if ((continuation_count == 2 && code_point < 0x800U) ||
        (continuation_count == 3 && code_point < 0x10000U) ||
        (code_point >= 0xD800U && code_point <= 0xDFFFU) || code_point > 0x10FFFFU) {
      return false;
    }
    i += continuation_count + 1U;
  }
  return true;
}

class Parser final {
 public:
  explicit Parser(std::string_view text) : text_(text) {}

  [[nodiscard]] bool parse(Json& out) {
    skip_ws();
    if (!parse_value(out)) {
      return false;
    }
    skip_ws();
    return pos_ == text_.size();
  }

 private:
  [[nodiscard]] bool parse_value(Json& out) {
    skip_ws();
    if (pos_ >= text_.size()) {
      return false;
    }
    const char c = text_[pos_];
    if (c == '"') {
      std::string value;
      if (!parse_string(value)) {
        return false;
      }
      out.value = std::move(value);
      return true;
    }
    if (c == '{') {
      return parse_object(out);
    }
    if (c == '[') {
      return parse_array(out);
    }
    if (c == '-' || std::isdigit(static_cast<unsigned char>(c)) != 0) {
      return parse_int(out);
    }
    return false;
  }

  [[nodiscard]] bool parse_object(Json& out) {
    ++pos_;
    Object object;
    std::set<std::string> keys;
    skip_ws();
    if (consume('}')) {
      out.value = std::move(object);
      return true;
    }
    while (true) {
      std::string key;
      if (!parse_string(key) || !keys.insert(key).second) {
        return false;
      }
      skip_ws();
      if (!consume(':')) {
        return false;
      }
      Json value;
      if (!parse_value(value)) {
        return false;
      }
      object.push_back({std::move(key), std::move(value)});
      skip_ws();
      if (consume('}')) {
        out.value = std::move(object);
        return true;
      }
      if (!consume(',')) {
        return false;
      }
    }
  }

  [[nodiscard]] bool parse_array(Json& out) {
    ++pos_;
    Array array;
    skip_ws();
    if (consume(']')) {
      out.value = std::move(array);
      return true;
    }
    while (true) {
      Json value;
      if (!parse_value(value)) {
        return false;
      }
      array.push_back(std::move(value));
      skip_ws();
      if (consume(']')) {
        out.value = std::move(array);
        return true;
      }
      if (!consume(',')) {
        return false;
      }
    }
  }

  [[nodiscard]] bool parse_string(std::string& out) {
    if (!consume('"')) {
      return false;
    }
    while (pos_ < text_.size()) {
      const unsigned char c = static_cast<unsigned char>(text_[pos_++]);
      if (c == '"') {
        return valid_utf8(out);
      }
      if (c == '\\') {
        if (pos_ >= text_.size()) {
          return false;
        }
        const char e = text_[pos_++];
        switch (e) {
          case '"':
          case '\\':
          case '/':
            out.push_back(e);
            break;
          case 'b':
            return false;
          case 'f':
            return false;
          case 'n':
            return false;
          case 'r':
            return false;
          case 't':
            return false;
          default:
            return false;
        }
      } else {
        if (c < 0x20U || c == 0U) {
          return false;
        }
        out.push_back(static_cast<char>(c));
      }
    }
    return false;
  }

  [[nodiscard]] bool parse_int(Json& out) {
    const std::size_t start = pos_;
    if (text_[pos_] == '-') {
      ++pos_;
    }
    if (pos_ >= text_.size() || std::isdigit(static_cast<unsigned char>(text_[pos_])) == 0) {
      return false;
    }
    while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_])) != 0) {
      ++pos_;
    }
    try {
      out.value = std::stoll(std::string(text_.substr(start, pos_ - start)));
      return true;
    } catch (...) {
      return false;
    }
  }

  void skip_ws() {
    while (pos_ < text_.size()) {
      const char c = text_[pos_];
      if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
        ++pos_;
      } else {
        break;
      }
    }
  }

  [[nodiscard]] bool consume(char c) {
    skip_ws();
    if (pos_ < text_.size() && text_[pos_] == c) {
      ++pos_;
      return true;
    }
    return false;
  }

  std::string_view text_;
  std::size_t pos_ = 0;
};

const Json* object_get(const Object& object, std::string_view key) {
  for (const auto& [row_key, value] : object) {
    if (row_key == key) {
      return &value;
    }
  }
  return nullptr;
}

const std::string* as_string(const Json* value) {
  if (value == nullptr) {
    return nullptr;
  }
  return std::get_if<std::string>(&value->value);
}

const Object* as_object(const Json* value) {
  if (value == nullptr) {
    return nullptr;
  }
  return std::get_if<Object>(&value->value);
}

const Array* as_array(const Json* value) {
  if (value == nullptr) {
    return nullptr;
  }
  return std::get_if<Array>(&value->value);
}

const std::int64_t* as_int(const Json* value) {
  if (value == nullptr) {
    return nullptr;
  }
  return std::get_if<std::int64_t>(&value->value);
}

bool valid_identifier(std::string_view text) {
  if (text.empty() || text.size() > 128U) {
    return false;
  }
  const auto valid_first = [](char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0;
  };
  const auto valid_rest = [](char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '.' || c == '_' || c == '-';
  };
  if (!valid_first(text[0])) {
    return false;
  }
  return std::all_of(text.begin() + 1, text.end(), valid_rest);
}

bool valid_display_name(std::string_view text) {
  if (text.empty() || text.size() > kMaxDisplayNameBytes) {
    return false;
  }
  return std::none_of(text.begin(), text.end(), [](char c) {
    const auto u = static_cast<unsigned char>(c);
    return u == 0U || u < 0x20U;
  });
}

std::string escape_json_string(std::string_view text) {
  std::string out;
  out.push_back('"');
  for (char c : text) {
    if (c == '"' || c == '\\') {
      out.push_back('\\');
    }
    out.push_back(c);
  }
  out.push_back('"');
  return out;
}

std::string canonical_json(const Json& value);

std::string canonical_object(const Object& object) {
  std::vector<std::pair<std::string, const Json*>> sorted;
  sorted.reserve(object.size());
  for (const auto& [key, value] : object) {
    sorted.push_back({key, &value});
  }
  std::sort(sorted.begin(), sorted.end(), [](const auto& lhs, const auto& rhs) {
    return lhs.first < rhs.first;
  });
  std::string out = "{";
  for (std::size_t i = 0; i < sorted.size(); ++i) {
    if (i != 0U) {
      out.push_back(',');
    }
    out += escape_json_string(sorted[i].first);
    out.push_back(':');
    out += canonical_json(*sorted[i].second);
  }
  out.push_back('}');
  return out;
}

std::string canonical_json(const Json& value) {
  if (const auto* s = std::get_if<std::string>(&value.value)) {
    return escape_json_string(*s);
  }
  if (const auto* i = std::get_if<std::int64_t>(&value.value)) {
    return std::to_string(*i);
  }
  if (const auto* object = std::get_if<Object>(&value.value)) {
    return canonical_object(*object);
  }
  if (const auto* array = std::get_if<Array>(&value.value)) {
    std::string out = "[";
    for (std::size_t i = 0; i < array->size(); ++i) {
      if (i != 0U) {
        out.push_back(',');
      }
      out += canonical_json((*array)[i]);
    }
    out.push_back(']');
    return out;
  }
  return "null";
}

bool beneath(const std::filesystem::path& child, const std::filesystem::path& parent) {
  const auto child_text = child.lexically_normal().string();
  const auto parent_text = parent.lexically_normal().string();
  return child_text == parent_text ||
         (child_text.size() > parent_text.size() && child_text.rfind(parent_text, 0) == 0 &&
          child_text[parent_text.size()] == std::filesystem::path::preferred_separator);
}

ManifestResult reject(Status status) {
  ManifestResult result;
  result.status = status;
  return result;
}

}  // namespace

ManifestResult load_manifest(const std::filesystem::path& path) {
  std::error_code ec;
  if (!std::filesystem::is_regular_file(path, ec) || ec) {
    return reject(Status::invalid_argument());
  }
  const auto size = std::filesystem::file_size(path, ec);
  if (ec || size > kMaxManifestBytes) {
    return reject(Status::resource_limit_exceeded());
  }

  std::ifstream input(path, std::ios::binary);
  std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  if (text.find('\0') != std::string::npos) {
    return reject(Status::invalid_argument());
  }

  Json root;
  Parser parser(text);
  if (!parser.parse(root)) {
    return reject(Status::invalid_argument());
  }
  const auto* object = std::get_if<Object>(&root.value);
  if (object == nullptr) {
    return reject(Status::invalid_argument());
  }

  const std::set<std::string_view> allowed{"schemaVersion", "databaseId", "databaseVersion", "triggers"};
  for (const auto& [key, ignored] : *object) {
    (void)ignored;
    if (!allowed.contains(key)) {
      return reject(Status::invalid_argument());
    }
  }

  const auto* schema = as_int(object_get(*object, "schemaVersion"));
  const auto* database_id = as_string(object_get(*object, "databaseId"));
  const auto* database_version = as_string(object_get(*object, "databaseVersion"));
  const auto* triggers = as_array(object_get(*object, "triggers"));
  if (schema == nullptr || *schema != 1 || database_id == nullptr || database_version == nullptr ||
      triggers == nullptr || !valid_identifier(*database_id) || !valid_identifier(*database_version) ||
      triggers->size() > kMaxTriggers) {
    return reject(Status::invalid_argument());
  }

  const std::filesystem::path manifest_dir = std::filesystem::canonical(path.parent_path(), ec);
  if (ec) {
    return reject(Status::invalid_argument());
  }

  ValidatedManifest manifest{
      .database_id = *database_id,
      .database_version = *database_version,
      .triggers = {},
  };
  std::set<std::string> trigger_ids;
  for (const Json& row : *triggers) {
    const auto* trigger = std::get_if<Object>(&row.value);
    if (trigger == nullptr) {
      return reject(Status::invalid_argument());
    }
    const auto* id = as_string(object_get(*trigger, "id"));
    const auto* display = as_string(object_get(*trigger, "displayName"));
    const auto* audio = as_string(object_get(*trigger, "audio"));
    const auto* metadata = as_object(object_get(*trigger, "metadata"));
    if (id == nullptr || display == nullptr || audio == nullptr || metadata == nullptr ||
        !valid_identifier(*id) || !trigger_ids.insert(*id).second || !valid_display_name(*display) ||
        audio->empty() || !is_supported_audio_extension(*audio)) {
      return reject(Status::invalid_argument());
    }

    const std::filesystem::path audio_path = std::filesystem::canonical(manifest_dir / *audio, ec);
    if (ec || !beneath(audio_path, manifest_dir) || !std::filesystem::is_regular_file(audio_path, ec) || ec) {
      return reject(Status::invalid_argument());
    }
    const auto audio_size = std::filesystem::file_size(audio_path, ec);
    if (ec || audio_size > kMaxAudioBytes) {
      return reject(Status::resource_limit_exceeded());
    }

    Json metadata_json;
    metadata_json.value = *metadata;
    std::string canonical_metadata = canonical_json(metadata_json);
    if (canonical_metadata.size() > kMaxMetadataBytes) {
      return reject(Status::resource_limit_exceeded());
    }

    manifest.triggers.push_back(ManifestTrigger{
        .id = *id,
        .display_name = *display,
        .audio_path = audio_path,
        .metadata_json = std::move(canonical_metadata),
    });
  }

  ManifestResult result;
  result.manifest = std::move(manifest);
  return result;
}

}  // namespace local_acr::cli
