#include <cerrno>
#include <cstdio>
#include <cstdlib>


//#include <SDKDDKVer.h>   // сам выставит _WIN32_WINNT под макс. доступную версию

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WIN32_WINNT 0x0600   // Windows Vista и новее
#include <windows.h>

#include "terminal_access.hpp"



static HANDLE hStdin = NULL;
static HANDLE hStdout = NULL;
static int savedConsoleOutputModeIsValid = 0;
static DWORD savedConsoleOutputMode = 0;
static int savedConsoleInputModeIsValid = 0;
static DWORD savedConsoleInputMode = 0;


static void disableRawMode(void) {
    printf("\x1b[0m");
    fflush(stdout);

    if (savedConsoleOutputModeIsValid)
        SetConsoleMode(hStdout, savedConsoleOutputMode);
    if (savedConsoleInputModeIsValid)
        SetConsoleMode(hStdin, savedConsoleInputMode);
}

namespace wkilocpp
{

    int enableRawMode(void) {
        // Make sure the console state will be returned to its original state
        // when this program ends
        atexit(disableRawMode);

        // Get handles for stdin and stdout
        hStdin = GetStdHandle(STD_INPUT_HANDLE);
        if (hStdin == INVALID_HANDLE_VALUE || hStdin == NULL)
            return 1;
        hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
        if (hStdout == INVALID_HANDLE_VALUE || hStdout == NULL)
            return 1;

        // Set console to "raw" mode

        if (!GetConsoleMode(hStdout, &savedConsoleOutputMode))
            return 1;
        savedConsoleOutputModeIsValid = 1;
        DWORD newOutputMode = savedConsoleOutputMode;
        newOutputMode |= ENABLE_PROCESSED_OUTPUT;
        newOutputMode &= ~ENABLE_WRAP_AT_EOL_OUTPUT;
        newOutputMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        if (!SetConsoleMode(hStdout, newOutputMode))
            return 1;
        if (!GetConsoleMode(hStdin, &savedConsoleInputMode))
            return 1;
        savedConsoleInputModeIsValid = 1;
        DWORD newInputMode = savedConsoleInputMode;
        newInputMode &= ~ENABLE_ECHO_INPUT;
        newInputMode &= ~ENABLE_LINE_INPUT;
        newInputMode &= ~ENABLE_PROCESSED_INPUT;
        newInputMode |= ENABLE_VIRTUAL_TERMINAL_INPUT;
        if (!SetConsoleMode(hStdin, newInputMode))
            return 1;

        return 0;
    }


    // https://stackoverflow.com/questions/6812224/getting-terminal-size-in-c-for-windows
    int getWindowSize(int* rows, int* cols) {
        CONSOLE_SCREEN_BUFFER_INFO csbi;

        GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
        *cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        *rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
        return 0;
    }

    // Following code uses the Windows api to read and write to console
    //  instead the C library functions
    //  below stuff works as expected.

    int winRead(int ignored, char* c, int toread) {
        unsigned long read = 0;
        ReadConsoleA(hStdin, c, toread, &read, NULL);
        return (int)read;
    }

    int winWrite(int ignored, const char* buf, int length) {
        unsigned long wrote = 0;
        WriteConsoleA(hStdout, buf, length, &wrote, NULL);
        return (int)wrote;
    }

    //  https://stackoverflow.com/a/47229318/1355145

    /* The original code is public domain -- Will Hartung 4/9/09 */
    /* Modifications, public domain as well, by Antti Haapala, 11/10/17
       - Switched to getc on 5/23/19 */

    
    ptrdiff_t getline(FILE* stream, std::string& line) 
    {
        size_t pos;
        int c;

        if (stream == NULL) {
            errno = EINVAL;
            return -1;
        }

        c = getc(stream);
        if (c == EOF) {
            return -1;
        }

        line.clear(); // always clear a line

        pos = 0;
        while (c != EOF) {
            
            line += static_cast<char>(c);

            
            if (c == '\n') {
                break;
            }
            c = getc(stream);
        }

        
        return 0;
    }

    // ==========

    wchar_t* yk_utf8_to_utf16_null_terminated(const char* str) {
        if (!str) return NULL;
        UINT cp = CP_UTF8;
        if (strlen(str) >= 3 && str[0] == (char)0xef && str[1] == (char)0xbb &&
            str[2] == (char)0xbf)
            str += 3;
        size_t pwcl = MultiByteToWideChar(cp, 0, str, -1, NULL, 0);
        wchar_t* pwcs = (wchar_t*)malloc(sizeof(wchar_t) * (pwcl + 1));
        pwcl = MultiByteToWideChar(cp, 0, str, -1, pwcs, static_cast<int>(pwcl + 1));
        pwcs[pwcl] = '\0';
        return pwcs;
    }

    int yk_io_writefile(const char* fpath, const char* data, size_t len) {
        wchar_t* wpath = yk_utf8_to_utf16_null_terminated(fpath);
        if (wpath == NULL) {
            return -1;
        }
#if defined(_MSC_VER)// MSVC
        FILE* file = nullptr;
        errno_t openerr = _wfopen_s(&file, wpath, L"wb+");
        if (0 != openerr) {
            if (NULL != file) {
                fclose(file);
            }
            free(wpath);
            return -1;
        }
        if (file == nullptr) {
            free(wpath);
            return -1;
        }
#else // GCC, MingW, etc
        FILE* file = _wfopen(wpath, L"wb+");
        if (file == nullptr) {
            free(wpath);
            return -1;
        }
#endif// msvc check
        size_t written = fwrite(data, sizeof(char), len, file);
        free(wpath);
        fclose(file);
        return (written == len) ? 0 : -2;
    }
} // wkilocpp namespace