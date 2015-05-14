/**
 * @file
 * Declares functions for debugging.
 */

#pragma once

#include <winsock2.h>
#include <windows.h>
#include <sstream>

#ifdef NDEBUG

#define OUTPUT_FMT_DEBUG_STRING(val) (void)0

#else

#define OUTPUT_FMT_DEBUG_STRING(val) \
    if (true) \
    { \
        std::ostringstream oss; \
        oss << "COMLIB: " << val << " (" << __FILE__ << " L" << __LINE__ << ")\r\n"; \
        OutputDebugStringA(oss.str().c_str()); \
    } \
    else \
        (void)0

#endif
