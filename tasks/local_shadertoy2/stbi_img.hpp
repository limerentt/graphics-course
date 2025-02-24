#pragma once

class stbi_img {
public:
  stbi_img(const char* name, int* width, int* height, int* comp_num_actual, int comp_num_required);

  ~stbi_img();

  unsigned char* data = nullptr;
};