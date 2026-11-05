#include "log_source.h"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>

#include "hal/security/hal_crypto.h"
#include "jb/file.h"
#include "lodepng.h"

namespace vp37 {

namespace {

bool ends_with(std::string_view text, std::string_view suffix) {
  return text.size() >= suffix.size() &&
         text.substr(text.size() - suffix.size()) == suffix;
}

uint32_t le32(const unsigned char *p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 |
         uint32_t(p[3]) << 24;
}

} // namespace

std::string read_file(const std::string &path, size_t max_bytes) {
  jb_file_data_t data{};
  const hal_status_t status = jb_file_read(path.c_str(), max_bytes, &data);
  if (status != HAL_OK) {
    throw Error("cannot read " + path + " (status " +
                std::to_string(int(status)) + ")");
  }
  std::string out(reinterpret_cast<const char *>(data.data), data.length);
  jb_file_release(&data);
  return out;
}

bool write_file(const std::string &path, std::string_view data) {
  bool changed = false;
  const hal_status_t status = jb_file_write_atomic(
      path.c_str(), data.data(), data.size(), false, &changed);
  if (status != HAL_OK) {
    throw Error("cannot write " + path + " (status " +
                std::to_string(int(status)) + ")");
  }
  return changed;
}

void write_output(const std::string &path, std::string_view data) {
  namespace fs = std::filesystem;
  std::error_code error;
  fs::create_directories(fs::path(path).parent_path(), error);
  const std::string temp =
      path + ".tmp" +
      std::to_string(std::hash<std::thread::id>()(std::this_thread::get_id()));
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    out.write(data.data(), std::streamsize(data.size()));
    if (!out)
      throw Error("cannot write " + temp);
  }
  fs::rename(temp, path, error);
  if (error) {
    fs::remove(temp, error);
    throw Error("cannot replace " + path);
  }
}

std::string gunzip(std::string_view data, size_t max_output) {
  const auto *in = reinterpret_cast<const unsigned char *>(data.data());
  const size_t size = data.size();
  if (size < 18 || in[0] != 0x1f || in[1] != 0x8b || in[2] != 8) {
    throw Error("not a gzip deflate stream");
  }
  const unsigned flags = in[3];
  if (flags & 0xe0)
    throw Error("gzip header has reserved flags");
  size_t pos = 10;
  const auto need = [&](size_t bytes) {
    if (pos + bytes > size - 8)
      throw Error("truncated gzip header");
  };
  if (flags & 0x04) { // FEXTRA
    need(2);
    const size_t extra = size_t(in[pos]) | size_t(in[pos + 1]) << 8;
    pos += 2;
    need(extra);
    pos += extra;
  }
  for (unsigned flag : {0x08u, 0x10u}) { // FNAME, FCOMMENT: NUL-terminated
    if (!(flags & flag))
      continue;
    while (true) {
      need(1);
      if (in[pos++] == 0)
        break;
    }
  }
  if (flags & 0x02) { // FHCRC
    need(2);
    pos += 2;
  }
  LodePNGDecompressSettings settings;
  lodepng_decompress_settings_init(&settings);
  settings.max_output_size = max_output;
  unsigned char *out = nullptr;
  size_t out_size = 0;
  const unsigned error =
      lodepng_inflate(&out, &out_size, in + pos, size - pos - 8, &settings);
  std::string text;
  if (error == 0)
    text.assign(reinterpret_cast<const char *>(out), out_size);
  std::free(out);
  if (error != 0) {
    throw Error(std::string("gzip inflate failed: ") +
                lodepng_error_text(error));
  }
  // lodepng does not report where the deflate stream ended. In a single
  // member it ends right before the 8-byte trailer, so the stream without
  // its last byte must not inflate; a second member (even an identical one,
  // whose trailer matches the first) or trailing data would.
  {
    unsigned char *probe = nullptr;
    size_t probe_size = 0;
    const unsigned short_error = lodepng_inflate(&probe, &probe_size, in + pos,
                                                 size - pos - 9, &settings);
    std::free(probe);
    if (short_error == 0)
      throw Error("gzip data continues after the first member");
  }
  const unsigned crc = lodepng_crc32(
      reinterpret_cast<const unsigned char *>(text.data()), text.size());
  if (crc != le32(in + size - 8) ||
      uint32_t(text.size()) != le32(in + size - 4)) {
    throw Error(
        "gzip CRC-32/length mismatch (damaged file or several members)");
  }
  return text;
}

std::string read_log(const std::string &path) {
  if (!ends_with(path, ".gz"))
    return read_file(path, kMaxLogBytes);
  try {
    return gunzip(read_file(path, kMaxLogBytes), kMaxLogBytes);
  } catch (const Error &error) {
    throw Error(path + ": " + error.what());
  }
}

std::string sha256_hex(std::string_view data) {
  hal_sha256_context_t context;
  uint8_t digest[HAL_SHA256_DIGEST_BYTES];
  if (hal_sha256_init_ex(&context) != HAL_OK ||
      hal_sha256_update_ex(&context,
                           reinterpret_cast<const uint8_t *>(data.data()),
                           data.size()) != HAL_OK ||
      hal_sha256_final_ex(&context, digest) != HAL_OK) {
    throw Error("SHA-256 failed");
  }
  static const char hex[] = "0123456789abcdef";
  std::string out;
  for (uint8_t byte : digest) {
    out.push_back(hex[byte >> 4]);
    out.push_back(hex[byte & 15]);
  }
  return out;
}

} // namespace vp37
