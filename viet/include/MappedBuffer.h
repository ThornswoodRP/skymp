#pragma once

#include <filesystem>

#ifdef WIN32
// THORNSWOOD (Thornswood #7444): this header goes into server_guest_lib's
// ZipDatabase.cpp, which compiles in a batch with other server sources
// (Thornswood #7247). Without WIN32_LEAN_AND_MEAN, Windows.h declares
// Winsock 1, which cannot share a file with the Winsock 2 that SLikeNet
// includes, and without NOMINMAX it defines min and max, which
// std::numeric_limits<T>::min() in the archives cannot follow. Measured on
// skymp #229's Windows build: batch 7 began with MongoDatabase.cpp and
// ZipDatabase.cpp and failed on both. Only HANDLE and the file mapping calls
// are used here, which WIN32_LEAN_AND_MEAN keeps.
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <Windows.h>
#endif

#include "IBuffer.h"

namespace Viet {

class MappedBuffer : public IBuffer
{
public:
  MappedBuffer(const std::filesystem::path& path);

  ~MappedBuffer();

  const char* GetData() const override;

  size_t GetLength() const override;

private:
#ifdef WIN32
  HANDLE fileHandle_ = nullptr;
  HANDLE mapHandle_ = nullptr;
  LPVOID viewPtr_ = nullptr;
#else
  int fd_ = -1;
#endif
  char* data_ = nullptr;
  size_t size_;
};

}
