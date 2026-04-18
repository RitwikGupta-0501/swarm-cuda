#pragma once

#ifdef _WIN32
  #include <direct.h>   // _mkdir on Windows
#else
  #include <sys/stat.h>
  #include <sys/types.h>
  #include <errno.h>
  static inline int _mkdir(const char* dir) {
      return mkdir(dir, (mode_t)0755);
  }
#endif
