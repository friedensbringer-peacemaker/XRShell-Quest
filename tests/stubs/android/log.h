// Test-Stub für <android/log.h>: Logzeilen auf stdout, wenn XRSHELL_TEST_LOG gesetzt ist.
#pragma once

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

enum { ANDROID_LOG_INFO = 4, ANDROID_LOG_ERROR = 6 };

inline int __android_log_print(int prio, const char* tag, const char* fmt, ...) {
    static const bool verbose = std::getenv("XRSHELL_TEST_LOG") != nullptr;
    if (!verbose) return 0;
    std::printf("[%s/%d] ", tag, prio);
    va_list args;
    va_start(args, fmt);
    std::vprintf(fmt, args);
    va_end(args);
    std::printf("\n");
    return 0;
}
