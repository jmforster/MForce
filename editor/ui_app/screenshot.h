#pragma once
#include <string>
#include <vector>

namespace mforce::ui {

// Writes `rgb` (width * height pixels, three bytes each, the bottom row
// first, as OpenGL reads them) to `path` as a 24-bit BMP. Throws
// std::runtime_error naming the file when it cannot be written.
void write_bmp(const std::string& path, int width, int height, const std::vector<unsigned char>& rgb);

}  // namespace mforce::ui
