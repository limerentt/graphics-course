#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include "stbi_img.hpp"

stbi_img::stbi_img(const char* name, int* width, int* height, int* comp_num_actual, int comp_num_required) {
    data = stbi_load(name, width, height, comp_num_actual, comp_num_required);
}

stbi_img::~stbi_img() {
    if (data != nullptr) {
        stbi_image_free(data);
    }
}
