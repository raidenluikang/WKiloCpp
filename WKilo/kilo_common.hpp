#pragma once

// common.h
#if defined(_MSC_VER)
    // Для MSVC __cpp_exceptions может быть определён даже при /EHs-,
    // поэтому ориентируемся только на _CPPUNWIND
#   if defined(_CPPUNWIND)
#       define APP_HAS_EXCEPTIONS 1
#   else
#       define APP_HAS_EXCEPTIONS 0
#   endif
#else
    // GCC/Clang корректно убирают эти макросы при -fno-exceptions
#   if defined(__cpp_exceptions) || defined(__EXCEPTIONS)
#       define APP_HAS_EXCEPTIONS 1
#   else
#       define APP_HAS_EXCEPTIONS 0
#   endif
#endif

