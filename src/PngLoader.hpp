#ifndef PNG_LOADER_HPP
#define PNG_LOADER_HPP

#include "Animation.hpp"
#include <string>


class PngLoader {
public:
  static Animation *loadPngAsAnimation(const std::string &path);
};

#endif // PNG_LOADER_HPP
