/* stb_image implementation unit for Aether (see third_party/README.md).
 * The configuration lives in third_party/CMakeLists.txt so that the header
 * sees the same options everywhere: only PNG and JPEG are compiled in, file
 * I/O is disabled (images are decoded from memory after the caller has
 * bounded their size), and dimensions are capped at 16384. */
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
