#ifndef VP37V_JSON_IO_H
#define VP37V_JSON_IO_H

// Thin C++ layer over the jhbase JSON parser and writer.

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "jb/json.h"
#include "jb/json_writer.h"

namespace vp37 {

// Borrowed node; the owning JsonDoc must outlive it. A missing member or
// item is an invalid node, and every read of it gives the fallback.
class Json {
public:
  Json() = default;
  explicit Json(jb_json_node_t node) : node_(node), valid_(true) {}

  bool valid() const { return valid_; }
  jb_json_type_t type() const;
  bool is_object() const { return valid_ && type() == JB_JSON_OBJECT; }
  bool is_array() const { return valid_ && type() == JB_JSON_ARRAY; }
  bool is_string() const { return valid_ && type() == JB_JSON_STRING; }
  bool is_number() const { return valid_ && type() == JB_JSON_NUMBER; }

  Json operator[](std::string_view key) const;
  Json at(size_t index) const;
  size_t size() const;
  std::vector<Json> items() const; // array items or object members
  std::string key() const;         // member name of an object member

  std::optional<double> number() const;
  std::string str(std::string_view fallback = "") const;
  std::optional<bool> boolean() const;
  std::string raw() const; // original JSON text of the value
  jb_json_node_t handle() const { return node_; }

private:
  jb_json_node_t node_{};
  bool valid_ = false;
};

class JsonDoc {
public:
  JsonDoc(const JsonDoc &) = delete;
  JsonDoc &operator=(const JsonDoc &) = delete;
  Json root() const { return Json(jb_json_node_t{&doc_, 0}); }

  // Throws Error with the line and column of the first failure.
  static std::unique_ptr<JsonDoc> parse(std::string text,
                                        const std::string &where);
  static std::unique_ptr<JsonDoc> load(const std::string &path);

private:
  JsonDoc() = default;
  std::string text_;
  std::vector<jb_json_token_t> tokens_;
  jb_json_doc_t doc_{};
};

// Streaming JSON text into a string. Keys and strings are escaped by jhbase.
class JsonWriter {
public:
  explicit JsonWriter(size_t max_keys = 8192);
  JsonWriter(const JsonWriter &) = delete;
  JsonWriter &operator=(const JsonWriter &) = delete;

  JsonWriter &begin_object();
  JsonWriter &end_object();
  JsonWriter &begin_array();
  JsonWriter &end_array();
  JsonWriter &key(std::string_view name);
  JsonWriter &string(std::string_view value);
  JsonWriter &number(double value); // NaN and infinities become null
  JsonWriter &integer(int64_t value);
  JsonWriter &boolean(bool value);
  JsonWriter &null();
  JsonWriter &node(const Json &value); // copies a parsed value, null if invalid
  // Pre-formatted number token (e.g. fixed decimals for series data).
  JsonWriter &number_token(std::string_view token);

  std::string finish();

private:
  void check(hal_status_t status);
  jb_json_writer_t writer_{};
  std::vector<unsigned char> scratch_;
  std::string text_;
};

} // namespace vp37

#endif
