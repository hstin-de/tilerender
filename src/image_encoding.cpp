#include "image_encoding.hpp"
#include <webp/encode.h>
#include <jpeglib.h>
#include <mbgl/util/premultiply.hpp>
#include <stdexcept>
#include <vector>

namespace mbgl {

    std::string encodeWebP(const PremultipliedImage& pre) {
        const auto src = util::unpremultiply(pre.clone());

        uint8_t* output_data = nullptr;
        size_t output_size = WebPEncodeRGBA(
            src.data.get(), src.size.width, src.size.height, src.stride(), 75.0f, &output_data);

        if (output_size == 0 || output_data == nullptr) {
            throw std::runtime_error("WebP encoding failed");
        }

        std::string webpData(reinterpret_cast<char*>(output_data), output_size);
        WebPFree(output_data);

        return webpData;
    }

    std::string encodeJPEG(const PremultipliedImage& pre) {
        const auto src = util::unpremultiply(pre.clone());

        struct jpeg_compress_struct cinfo;
        struct jpeg_error_mgr jerr;

        unsigned char* mem = nullptr;
        unsigned long mem_size = 0;

        cinfo.err = jpeg_std_error(&jerr);
        jpeg_create_compress(&cinfo);

        jpeg_mem_dest(&cinfo, &mem, &mem_size);

        cinfo.image_width = src.size.width;
        cinfo.image_height = src.size.height;
        cinfo.input_components = 3; // RGB
        cinfo.in_color_space = JCS_RGB;

        jpeg_set_defaults(&cinfo);
        jpeg_set_quality(&cinfo, 75, TRUE);

        jpeg_start_compress(&cinfo, TRUE);

        std::vector<JSAMPLE> row_buffer(src.size.width * 3);
        while (cinfo.next_scanline < cinfo.image_height) {
            const uint8_t* src_row = src.data.get() + cinfo.next_scanline * src.stride();
            for (size_t i = 0; i < src.size.width; ++i) {
                row_buffer[i * 3 + 0] = src_row[i * 4 + 0]; // R
                row_buffer[i * 3 + 1] = src_row[i * 4 + 1]; // G
                row_buffer[i * 3 + 2] = src_row[i * 4 + 2]; // B
            }

            JSAMPROW row_pointer = &row_buffer[0];
            jpeg_write_scanlines(&cinfo, &row_pointer, 1);
        }

        jpeg_finish_compress(&cinfo);
        jpeg_destroy_compress(&cinfo);

        std::string jpegData(reinterpret_cast<char*>(mem), mem_size);
        free(mem);

        return jpegData;
    }

} // namespace mbgl


std::string imageString(ImageFormat format)
{
    switch (format)
    {
    case ImageFormat::PNG:
        return "png";
    case ImageFormat::JPEG:
        return "jpg";
    case ImageFormat::WEBP:
        return "webp";
    default:
        throw std::runtime_error("Invalid image format");
    }
}