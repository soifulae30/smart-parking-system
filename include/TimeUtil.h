#ifndef TIME_UTIL_H
#define TIME_UTIL_H

#include <string>

// Current local time as "YYYY-MM-DD HH:MM:SS". Thread-safe.
std::string currentTimestamp();

#endif // TIME_UTIL_H
