#include <stb_image.h>

#include <climits>
#include <cstring>

#include "aether/assets/model.hpp"

namespace aether::assets {

Result<TextureImage> decode_image(std::span<const std::uint8_t> bytes, std::uint32_t max_size) {
    if (bytes.empty() || bytes.size() > static_cast<std::size_t>(INT_MAX))
        return Error{ErrorCode::InvalidArgument, "image data is empty or too large"};
    const auto* data = reinterpret_cast<const stbi_uc*>(bytes.data());
    const int len = static_cast<int>(bytes.size());
    int w = 0, h = 0, comp = 0;
    // Check dimensions before decoding so hostile files cannot request huge allocations.
    if (!stbi_info_from_memory(data, len, &w, &h, &comp))
        return Error{ErrorCode::Unsupported, std::string("unsupported or corrupt image (") + stbi_failure_reason() + ")"};
    if (w <= 0 || h <= 0 || static_cast<std::uint32_t>(w) > max_size || static_cast<std::uint32_t>(h) > max_size)
        return Error{ErrorCode::InvalidArgument, "image is " + std::to_string(w) + "x" + std::to_string(h) +
                                                     ", larger than the " + std::to_string(max_size) + " limit"};
    stbi_uc* pixels = stbi_load_from_memory(data, len, &w, &h, &comp, 4);
    if (!pixels) return Error{ErrorCode::InvalidArgument, std::string("image decode failed (") + stbi_failure_reason() + ")"};
    TextureImage img;
    img.width = static_cast<std::uint32_t>(w);
    img.height = static_cast<std::uint32_t>(h);
    img.rgba.assign(pixels, pixels + static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
    stbi_image_free(pixels);
    return img;
}

}  // namespace aether::assets
