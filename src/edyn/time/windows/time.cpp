#include "edyn/time/time.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#ifndef __MINGW32__
#include <timeapi.h>
#else
#include <mmsystem.h>
#endif

namespace edyn {

uint32_t ticks() {
    return static_cast<uint32_t>(timeGetTime());
}

void delay(uint32_t ms) {
    Sleep(static_cast<DWORD>(ms));
}

uint64_t performance_counter() {
    LARGE_INTEGER ticks;
    if (QueryPerformanceCounter(&ticks)) {
        return ticks.QuadPart;
    }
    return 0;
}

uint64_t performance_frequency() {
    LARGE_INTEGER freq;
    if (QueryPerformanceFrequency(&freq)) {
        return freq.QuadPart;
    }
    return 0;
}

}
