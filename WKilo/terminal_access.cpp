
#include <cstdio>
#include <cstdlib>

#include <system_error>


#include <string_view>
#include <optional>
#include <utility>
#include <algorithm>

//#include <SDKDDKVer.h>   // сам выставит _WIN32_WINNT под макс. доступную версию

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WIN32_WINNT 0x0600   // Windows Vista и новее
#include <windows.h>

#include "terminal_access.hpp"



namespace wkilocpp
{
    struct ScreenHandle::impl
    {
        HANDLE hStdin;
        HANDLE hStdout;

        std::optional<DWORD> savedConsoleOutputMode;
        std::optional<DWORD> savedConsoleInputMode;

        void disableRawMode()
        {
            printf("\x1b[0m");
            fflush(stdout);

            if (savedConsoleOutputMode.has_value())
            {
                SetConsoleMode(hStdout, *savedConsoleOutputMode);
                savedConsoleOutputMode = std::nullopt;
            }

            if (savedConsoleInputMode.has_value())
            {
                SetConsoleMode(hStdin, *savedConsoleInputMode);
                savedConsoleInputMode = std::nullopt;
            }

            printf("\nBye!\n");
            fflush(stdout);
        }
    };
    
    ScreenHandle::ScreenHandle() 
        : d_(new impl{})
    {
        d_->hStdin = GetStdHandle(STD_INPUT_HANDLE);

        if (d_->hStdin == INVALID_HANDLE_VALUE || d_->hStdin == NULL)
        {
            throw std::system_error(GetLastError(), std::system_category(), "GetStdHandle(STD_INPUT_HANDLE) failed");
        }

        d_->hStdout = GetStdHandle(STD_OUTPUT_HANDLE);

        if (d_->hStdout == INVALID_HANDLE_VALUE || d_->hStdout == NULL)
        {
            throw std::system_error(GetLastError(), std::system_category(), "GetStdHandle(STD_OUTPUT_HANDLE) failed");
        }
    }

    ScreenHandle::~ScreenHandle()
    {
        
        d_->disableRawMode();
        delete d_;
        
    }

    
    void ScreenHandle::enableRawMode(void)
    {
        // Get handles for stdin and stdout
        // Set console to "raw" mode
        DWORD outputMode = 0;
        if (!GetConsoleMode(d_->hStdout, &outputMode))
        {
            throw std::system_error(GetLastError(), std::system_category(), "GetConsoleMode(hStdout) failed");
        }

        d_->savedConsoleOutputMode = outputMode;
        DWORD newOutputMode = outputMode;
        newOutputMode |= ENABLE_PROCESSED_OUTPUT;
        newOutputMode &= ~ENABLE_WRAP_AT_EOL_OUTPUT;
        newOutputMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;

        if (!SetConsoleMode(d_->hStdout, newOutputMode))
        {
            throw std::system_error(GetLastError(), std::system_category(), "SetConsoleMode(hStdout, newOutputMode) failed");
        }


        //-----------------------------------------------------------
        DWORD inputMode = 0;
        if (!GetConsoleMode(d_->hStdin, &inputMode))
        {
            throw std::system_error(GetLastError(), std::system_category(), "GetConsoleMode(hStdin) failed");
        }

        d_->savedConsoleInputMode = inputMode;
        DWORD newInputMode = inputMode;
        newInputMode &= ~ENABLE_ECHO_INPUT;
        newInputMode &= ~ENABLE_LINE_INPUT;
        newInputMode &= ~ENABLE_PROCESSED_INPUT;
        newInputMode |= ENABLE_VIRTUAL_TERMINAL_INPUT;

        if (!SetConsoleMode(d_->hStdin, newInputMode))
        {
            throw std::system_error(GetLastError(), std::system_category(), "SetConsoleMode(hStdin, newInputMode) failed");
        }
    }

    

    
    // https://stackoverflow.com/questions/6812224/getting-terminal-size-in-c-for-windows
    ScreenSize ScreenHandle::getWindowSize() const
    {
        CONSOLE_SCREEN_BUFFER_INFO csbi{};
     
        //У нас d_ != nullptr and d_->hStdout != NULL and d_->hStdout != INVALID_HANDLE_VALUE.

        HANDLE const stdHandle = d_->hStdout;

        //https://learn.microsoft.com/en-us/windows/console/getconsolescreenbufferinfo
        BOOL const bOk = GetConsoleScreenBufferInfo(stdHandle, &csbi);
        
        if (!bOk) 
        {
            //zero 
            //If the function fails, the return value is zero. To get extended error information, call GetLastError.

            //return ScreenSize{ .rows = -3, .cols = -3 };
            throw std::system_error( ::GetLastError(), std::system_category(), "GetConsoleScreenBufferInfo failed");
        }
        
        const int col_size = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        const int row_size = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;

        return ScreenSize{ .rows = row_size, .cols = col_size };
    }

    // Following code uses the Windows api to winRead and winWrite to console
    //  instead the C library functions
    //  below stuff works as expected.

    int ScreenHandle::winRead( /*int ignored,*/ std::span<char> buf)
    {
        DWORD read = 0;
        BOOL bOk = ReadConsoleA(d_->hStdin, buf.data(), buf.size(), &read, NULL);
        
        if (!bOk) 
        {
            return -1;
        }
        
        return (int)read;
    }

    int ScreenHandle::winWrite( /*int ignored,*/ std::span<const char> cbuf)
    {
        DWORD wrote = 0;
        BOOL bOk = WriteConsoleA(d_->hStdout, cbuf.data(), static_cast<DWORD>(cbuf.size()), &wrote, NULL);
        
        if (!bOk) 
        {
            return -1;
        }
        
        return (int)wrote;
    }

    unsigned long winGetLastError() 
    {
        return ::GetLastError();
    }


    

} // wkilocpp namespace