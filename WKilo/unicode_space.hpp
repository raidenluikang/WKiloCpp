
// unicode_space.hpp
#pragma once

namespace unicode {

    constexpr bool is_space(char32_t c) noexcept {
        switch (c) {
        case 0x0009: case 0x000A: case 0x000B: case 0x000C: case 0x000D:
        case 0x0020: case 0x0085: case 0x00A0: case 0x1680:
        case 0x2000: case 0x2001: case 0x2002: case 0x2003: case 0x2004:
        case 0x2005: case 0x2006: case 0x2007: case 0x2008: case 0x2009:
        case 0x200A: case 0x2028: case 0x2029: case 0x202F: case 0x205F:
        case 0x3000:
            return true;
        default:
            return false;
        }
    }

    // Тесты компилируются в бинарник как no-op — если static_assert не упадёт при компиляции, всё верно
    static_assert(is_space(U' '));
    static_assert(is_space(U'\t'));
    static_assert(is_space(U'\u3000'));   // IDEOGRAPHIC SPACE
    static_assert(is_space(U'\u00A0'));   // NO-BREAK SPACE
    static_assert(!is_space(U'A'));
    static_assert(!is_space(U'0'));
    static_assert(!is_space(U'\0'));


    constexpr bool is_control(char32_t c) noexcept 
    {
        return (c <= 0x1F) || (c == 0x7F) || (c >= 0x80 && c <= 0x9F);
    }

    static_assert(is_control(U'\t'));    // 0x09
    static_assert(is_control(U'\n'));    // 0x0A
    static_assert(is_control(0x7F));     // DEL
    static_assert(is_control(0x85));     // NEL (в C1-диапазоне)
    static_assert(!is_control(U'A'));
    static_assert(!is_control(U' '));    // пробел — НЕ control (это как раз isspace)

} // namespace unicode