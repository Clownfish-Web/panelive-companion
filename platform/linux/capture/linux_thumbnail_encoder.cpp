#include "linux_thumbnail_encoder.hpp"

#include <jpeglib.h>

#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <vector>

namespace studio_remote::linux
{
EncodedThumbnail LinuxThumbnailEncoder::encode(const RawThumbnail& image)
{
  if (image.width == 0 || image.height == 0 || image.stride < image.width * 4 ||
      image.bgra.size() < static_cast<std::size_t>(image.stride) * image.height)
    throw std::invalid_argument("Invalid raw thumbnail");

  jpeg_compress_struct compressor{};
  jpeg_error_mgr errors{};
  std::unique_ptr<unsigned char, decltype(&std::free)> output_owner(nullptr, &std::free);
  compressor.err = jpeg_std_error(&errors);
  jpeg_create_compress(&compressor);
  struct CompressorCleanup
  {
    jpeg_compress_struct* compressor;
    ~CompressorCleanup()
    {
      jpeg_destroy_compress(compressor);
    }
  } cleanup{&compressor};

  unsigned char* output = nullptr;
  unsigned long output_size = 0;
  jpeg_mem_dest(&compressor, &output, &output_size);
  compressor.image_width = image.width;
  compressor.image_height = image.height;
  compressor.input_components = 3;
  compressor.in_color_space = JCS_RGB;
  jpeg_set_defaults(&compressor);
  jpeg_set_quality(&compressor, 85, TRUE);
  jpeg_start_compress(&compressor, TRUE);

  std::vector<std::uint8_t> row(static_cast<std::size_t>(image.width) * 3);
  while (compressor.next_scanline < compressor.image_height)
  {
    const auto source_row = static_cast<std::size_t>(compressor.next_scanline) * image.stride;
    for (std::uint32_t column = 0; column < image.width; ++column)
    {
      const auto source = source_row + static_cast<std::size_t>(column) * 4;
      const auto target = static_cast<std::size_t>(column) * 3;
      row[target] = image.bgra[source + 2];
      row[target + 1] = image.bgra[source + 1];
      row[target + 2] = image.bgra[source];
    }
    JSAMPROW row_pointer = row.data();
    jpeg_write_scanlines(&compressor, &row_pointer, 1);
  }
  jpeg_finish_compress(&compressor);
  output_owner.reset(output);
  if (!output || output_size == 0)
    throw std::runtime_error("Encoded JPEG image is empty");
  return {"image/jpeg", std::vector<std::uint8_t>(output, output + output_size)};
}

} // namespace studio_remote::linux
