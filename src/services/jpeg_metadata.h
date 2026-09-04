#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace service::camera_backend {

struct ExifMetadata {
  std::string make{"M5Stack"};
  std::string model{"CardputerZero IMX219"};
  std::string software{"Camera 0.3.7"};
  std::string date_time_original;
  std::string user_comment;
  int width{0};
  int height{0};
  std::optional<int32_t> exposure_time_us;
  std::optional<uint16_t> iso_speed;
  std::optional<int32_t> brightness_value;
  std::optional<int32_t> exposure_bias_value;
  std::optional<uint16_t> metering_mode;
  std::optional<uint16_t> light_source;
  std::optional<uint32_t> f_number_x100;
  std::optional<uint32_t> focal_length_mm_x100;
  std::string lens_make;
  std::string lens_model;
};

ExifMetadata make_default_exif_metadata(int width, int height);
std::vector<uint8_t> build_exif_app1(const ExifMetadata& metadata);

}  // namespace service::camera_backend
