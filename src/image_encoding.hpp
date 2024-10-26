#ifndef IMAGE_ENCODING_HPP
#define IMAGE_ENCODING_HPP

#include <string>
#include <mbgl/util/image.hpp>

namespace mbgl {

    std::string encodeWebP(const PremultipliedImage& image);

    std::string encodeJPEG(const PremultipliedImage& image);

} // namespace mbgl

enum class ImageFormat {
    PNG,
    JPEG,
    WEBP
};

std::string imageString(ImageFormat format);

#endif // IMAGE_ENCODING_HPP
