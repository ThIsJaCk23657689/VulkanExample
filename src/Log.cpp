#include "Log.hpp"

#include <cstdarg>
#include <iostream>

void Log::Message(const char *fmt, ...) {
    char buffer[m_MessageBufferSize];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, std::size(buffer), fmt, args);
    va_end(args);

    std::cout << "[Message] " << buffer << std::endl;
}