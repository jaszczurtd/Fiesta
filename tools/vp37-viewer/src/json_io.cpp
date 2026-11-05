#include "json_io.h"

#include <cmath>
#include <cstdint>

#include "jb/json_number.h"
#include "log_source.h"

namespace vp37 {

namespace {

jb_json_span_t span(std::string_view text) {
  return {text.data(), text.size()};
}

hal_status_t append(void *user, const char *data, size_t length) {
  static_cast<std::string *>(user)->append(data, length);
  return HAL_OK;
}

constexpr size_t kMaxJsonBytes = size_t(256) << 20;

} // namespace

jb_json_type_t Json::type() const {
  jb_json_type_t type = JB_JSON_NULL;
  if (valid_)
    (void)jb_json_type(node_, &type);
  return type;
}

Json Json::operator[](std::string_view key) const {
  jb_json_node_t out;
  if (!is_object() || jb_json_find(node_, span(key), &out) != HAL_OK)
    return Json();
  return Json(out);
}

Json Json::at(size_t index) const {
  jb_json_node_t out;
  if (!is_array() || jb_json_at(node_, index, &out) != HAL_OK)
    return Json();
  return Json(out);
}

size_t Json::size() const {
  size_t count = 0;
  if (is_array() || is_object())
    (void)jb_json_count(node_, &count);
  return count;
}

std::vector<Json> Json::items() const {
  std::vector<Json> out;
  if (!is_array() && !is_object())
    return out;
  jb_json_node_t item;
  if (jb_json_first(node_, &item) != HAL_OK)
    return out;
  while (true) {
    out.emplace_back(item);
    if (jb_json_next(item, &item) != HAL_OK)
      break;
  }
  return out;
}

std::string Json::key() const {
  // (NULL, 0) measures; a non-empty name then needs a second call.
  size_t required = 0;
  const hal_status_t status =
      valid_ ? jb_json_key_copy(node_, nullptr, 0, &required) : HAL_EINVAL;
  if ((status != HAL_OK && status != HAL_EOVERFLOW) || required == 0)
    return std::string();
  std::string out(required, '\0');
  if (jb_json_key_copy(node_, out.data(), out.size(), &required) != HAL_OK)
    return std::string();
  return out;
}

std::optional<double> Json::number() const {
  if (!is_number())
    return std::nullopt;
  size_t scratch_bytes = 0;
  if (jb_json_float_workspace_size(node_, 64, &scratch_bytes) != HAL_OK)
    return std::nullopt;
  std::vector<unsigned char> scratch(scratch_bytes);
  const jb_json_float_options_t options{false, true, 1u << 20};
  double value;
  if (jb_json_get_f64(node_, &options, {scratch.data(), scratch.size()},
                      &value) != HAL_OK) {
    return std::nullopt;
  }
  return value;
}

std::string Json::str(std::string_view fallback) const {
  size_t required = 0;
  if (!is_string())
    return std::string(fallback);
  const hal_status_t status = jb_json_string_copy(node_, nullptr, 0, &required);
  if (status != HAL_OK && status != HAL_EOVERFLOW)
    return std::string(fallback);
  if (required == 0)
    return std::string();
  std::string out(required, '\0');
  if (jb_json_string_copy(node_, out.data(), out.size(), &required) != HAL_OK) {
    return std::string(fallback);
  }
  return out;
}

std::optional<bool> Json::boolean() const {
  bool value;
  if (!valid_ || jb_json_get_bool(node_, &value) != HAL_OK)
    return std::nullopt;
  return value;
}

std::string Json::raw() const {
  jb_json_span_t out;
  if (!valid_ || jb_json_raw(node_, &out) != HAL_OK)
    return "null";
  return std::string(out.data, out.length);
}

std::unique_ptr<JsonDoc> JsonDoc::parse(std::string text,
                                        const std::string &where) {
  std::unique_ptr<JsonDoc> doc(new JsonDoc());
  doc->text_ = std::move(text);
  const jb_json_span_t input = span(doc->text_);
  const size_t limit = doc->text_.size() + 1;
  const jb_json_limits_t limits{limit,    50000000,         256, limit, 4096,
                                50000000, limit * 64 + 4096};
  size_t scratch_bytes = 0;
  if (jb_json_parse_workspace_size(&limits, &scratch_bytes) != HAL_OK) {
    throw Error(where + ": JSON workspace overflow");
  }
  std::vector<unsigned char> scratch(scratch_bytes);
  jb_json_requirements_t needed{};
  jb_json_error_t error{};
  const auto fail = [&](const char *what) {
    size_t line = 0, column = 0;
    (void)jb_json_locate(input, error.offset, &line, &column);
    throw Error(where + ":" + std::to_string(line) + ":" +
                std::to_string(column) + ": " + what);
  };
  if (jb_json_measure(input, &limits, {scratch.data(), scratch.size()}, &needed,
                      &error) != HAL_OK) {
    fail("invalid JSON");
  }
  doc->tokens_.resize(needed.node_count);
  if (jb_json_parse(input, &limits, doc->tokens_.data(), doc->tokens_.size(),
                    {scratch.data(), scratch.size()}, &doc->doc_,
                    &error) != HAL_OK) {
    fail("invalid JSON");
  }
  return doc;
}

std::unique_ptr<JsonDoc> JsonDoc::load(const std::string &path) {
  return parse(read_file(path, kMaxJsonBytes), path);
}

JsonWriter::JsonWriter(size_t max_keys) {
  // Keys are counted over the objects open at the same time; the duplicate
  // check budget covers the whole document, and our own output is trusted.
  const jb_json_writer_limits_t limits{size_t(1) << 31, 64, max_keys,
                                       max_keys * 64, SIZE_MAX / 2};
  size_t needed = 0;
  check(jb_json_writer_workspace_size(&limits, &needed));
  scratch_.resize(needed);
  check(jb_json_writer_init(
      &writer_, &limits, {scratch_.data(), scratch_.size()}, append, &text_));
}

void JsonWriter::check(hal_status_t status) {
  if (status != HAL_OK)
    throw Error("JSON writer failed (status " + std::to_string(int(status)) +
                ")");
}

JsonWriter &JsonWriter::begin_object() {
  check(jb_json_writer_object_begin(&writer_));
  return *this;
}
JsonWriter &JsonWriter::end_object() {
  check(jb_json_writer_object_end(&writer_));
  return *this;
}
JsonWriter &JsonWriter::begin_array() {
  check(jb_json_writer_array_begin(&writer_));
  return *this;
}
JsonWriter &JsonWriter::end_array() {
  check(jb_json_writer_array_end(&writer_));
  return *this;
}
JsonWriter &JsonWriter::key(std::string_view name) {
  check(jb_json_writer_key(&writer_, span(name)));
  return *this;
}
JsonWriter &JsonWriter::string(std::string_view value) {
  check(jb_json_writer_string(&writer_, span(value)));
  return *this;
}
JsonWriter &JsonWriter::number(double value) {
  if (!std::isfinite(value))
    return null();
  check(jb_json_writer_f64(&writer_, value));
  return *this;
}
JsonWriter &JsonWriter::integer(int64_t value) {
  check(jb_json_writer_i64(&writer_, value));
  return *this;
}
JsonWriter &JsonWriter::boolean(bool value) {
  check(jb_json_writer_bool(&writer_, value));
  return *this;
}
JsonWriter &JsonWriter::null() {
  check(jb_json_writer_null(&writer_));
  return *this;
}
JsonWriter &JsonWriter::node(const Json &value) {
  if (!value.valid())
    return null();
  check(jb_json_writer_node(&writer_, value.handle()));
  return *this;
}
JsonWriter &JsonWriter::number_token(std::string_view token) {
  check(jb_json_writer_number(&writer_, span(token)));
  return *this;
}

std::string JsonWriter::finish() {
  size_t bytes = 0;
  check(jb_json_writer_finish(&writer_, &bytes));
  return std::move(text_);
}

} // namespace vp37
