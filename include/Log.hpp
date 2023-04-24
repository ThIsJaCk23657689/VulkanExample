#ifndef LOG_HPP
#define LOG_HPP

#include <cstddef>

class Log {
public:
    Log() = delete;
    ~Log() = delete;

    static void Message(const char* fmt...);

private:
    static constexpr size_t m_MessageBufferSize = 4096;

};

#endif
