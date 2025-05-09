#pragma once

#include "LinuxCOM.h"
#include <cstring>

// Define comparison operators for REFIID objects
inline bool operator==(const REFIID& lhs, const REFIID& rhs) {
    return std::memcmp(&lhs, &rhs, sizeof(REFIID)) == 0;
}

inline bool operator!=(const REFIID& lhs, const REFIID& rhs) {
    return !(lhs == rhs);
}