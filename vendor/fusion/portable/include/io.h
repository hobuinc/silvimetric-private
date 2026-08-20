#pragma once
#include "afx.h"

inline void _splitpath(
    const char* source, char* drive, char* directory, char* filename, char* extension
) {
    const std::filesystem::path path(source ? source : "");
    if (drive) drive[0] = '\0';
    if (directory) std::snprintf(directory, _MAX_PATH, "%s", path.parent_path().string().c_str());
    if (directory && directory[0] && directory[std::strlen(directory) - 1] != '/') std::strcat(directory, "/");
    if (filename) std::snprintf(filename, _MAX_FNAME, "%s", path.stem().string().c_str());
    if (extension) std::snprintf(extension, _MAX_EXT, "%s", path.extension().string().c_str());
}
