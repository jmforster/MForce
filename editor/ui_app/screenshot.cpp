#include "screenshot.h"
#include <cstdint>
#include <fstream>
#include <stdexcept>

namespace mforce::ui {

namespace {

// Little-endian, as a BMP header holds its numbers.
void put16(std::ofstream& out, std::uint16_t v) {
  const char b[2] = {char(v & 0xFF), char(v >> 8)};
  out.write(b, 2);
}

void put32(std::ofstream& out, std::uint32_t v) {
  const char b[4] = {char(v & 0xFF), char((v >> 8) & 0xFF), char((v >> 16) & 0xFF), char(v >> 24)};
  out.write(b, 4);
}

// The file header and the BITMAPINFOHEADER of a 24-bit picture whose rows
// run bottom to top (a positive height), each padded to four bytes.
void put_headers(std::ofstream& out, int width, int height, std::uint32_t rowBytes) {
  const std::uint32_t pixels = rowBytes * std::uint32_t(height);
  out.write("BM", 2);
  put32(out, 54 + pixels);
  put32(out, 0);
  put32(out, 54);                                   // where the pixels begin
  put32(out, 40);                                   // the info header's size
  put32(out, std::uint32_t(width));
  put32(out, std::uint32_t(height));
  put16(out, 1);                                    // planes
  put16(out, 24);                                   // bits per pixel
  put32(out, 0);                                    // no compression
  put32(out, pixels);
  put32(out, 2835);                                 // 72 dots per inch, as metres
  put32(out, 2835);
  put32(out, 0);
  put32(out, 0);
}

}  // namespace

void write_bmp(const std::string& path, int width, int height, const std::vector<unsigned char>& rgb) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  const std::uint32_t rowBytes = (std::uint32_t(width) * 3 + 3) & ~3u;
  put_headers(out, width, height, rowBytes);
  std::vector<char> row(rowBytes, 0);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const size_t i = (size_t(y) * size_t(width) + size_t(x)) * 3;
      row[size_t(x) * 3] = char(rgb[i + 2]);         // a BMP keeps blue, green, red
      row[size_t(x) * 3 + 1] = char(rgb[i + 1]);
      row[size_t(x) * 3 + 2] = char(rgb[i]);
    }
    out.write(row.data(), std::streamsize(row.size()));
  }
  out.close();
  if (!out) throw std::runtime_error(path + ": cannot write the picture");
}

}  // namespace mforce::ui
