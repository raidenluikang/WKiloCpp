


#include "terminal_access.hpp"
#include "kilo_common.hpp"


#include <cstdio>
#include <cstdlib>

#if APP_HAS_EXCEPTIONS

#include <system_error>

#endif //!APP_HAS_EXCEPTIONS

#include <string_view>
#include <optional>
#include <utility>
#include <algorithm>

//#include <SDKDDKVer.h>   // сам выставит _WIN32_WINNT под макс. доступную версию

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WIN32_WINNT 0x0600   // Windows Vista и новее
#include <windows.h>



namespace wkilocpp
{
    

    struct ScreenHandle::impl
    {
        HANDLE hStdin;
        HANDLE hStdout;

        bool rawModeEnabled = false;
        std::optional<DWORD> savedConsoleOutputMode;
        std::optional<DWORD> savedConsoleInputMode;

        void disableRawMode()
        {
            //Защита от двойной вызов.
            if (rawModeEnabled) 
            {
                rawModeEnabled = false;
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
        }
    };

    void throw_or_abort(const char* msg, ScreenHandle::impl* d_) {
#if APP_HAS_EXCEPTIONS
        throw std::system_error(GetLastError(), std::system_category(), msg);
#else 
        if (d_ != nullptr) 
        {
            d_->disableRawMode();
        }
        std::fprintf(stderr, "%s. ErrorCode: %d\n", msg, GetLastError());
        std::fflush(stderr);
        std::abort();
#endif 
    }
    
    void ScreenHandle::disableRawMode() 
    {
        d_->disableRawMode();
    }

    ScreenHandle::ScreenHandle() 
        : d_(new impl{})
    {
        d_->hStdin = GetStdHandle(STD_INPUT_HANDLE);

        if (d_->hStdin == INVALID_HANDLE_VALUE || d_->hStdin == NULL)
        {
            throw_or_abort("GetStdHandle(STD_INPUT_HANDLE) failed", d_);
        }

        d_->hStdout = GetStdHandle(STD_OUTPUT_HANDLE);

        if (d_->hStdout == INVALID_HANDLE_VALUE || d_->hStdout == NULL)
        {
            throw_or_abort("GetStdHandle(STD_OUTPUT_HANDLE) failed", d_);
        }
    }

    ScreenHandle::~ScreenHandle()
    {
        d_->disableRawMode();
        delete d_;
    }

    
    void ScreenHandle::enableRawMode(void)
    {
        d_->rawModeEnabled = true; 

        // Get handles for stdin and stdout
        // Set console to "raw" mode
        DWORD outputMode = 0;
        if (!GetConsoleMode(d_->hStdout, &outputMode))
        {
            throw_or_abort("GetConsoleMode(hStdout) failed", d_);
        }

        d_->savedConsoleOutputMode = outputMode;
        DWORD newOutputMode = outputMode;
        newOutputMode |= ENABLE_PROCESSED_OUTPUT;
        newOutputMode &= ~ENABLE_WRAP_AT_EOL_OUTPUT;
        newOutputMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;

        if (!SetConsoleMode(d_->hStdout, newOutputMode))
        {
            throw_or_abort("SetConsoleMode(hStdout, newOutputMode) failed", d_);
        }


        //-----------------------------------------------------------
        DWORD inputMode = 0;
        if (!GetConsoleMode(d_->hStdin, &inputMode))
        {
            throw_or_abort("GetConsoleMode(hStdin) failed", d_);
        }

        d_->savedConsoleInputMode = inputMode;
        DWORD newInputMode = inputMode;
        newInputMode &= ~ENABLE_ECHO_INPUT;
        newInputMode &= ~ENABLE_LINE_INPUT;
        newInputMode &= ~ENABLE_PROCESSED_INPUT;
        newInputMode |= ENABLE_VIRTUAL_TERMINAL_INPUT;

        if (!SetConsoleMode(d_->hStdin, newInputMode))
        {
            throw_or_abort("SetConsoleMode(hStdin, newInputMode) failed", d_);
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
            throw_or_abort("GetConsoleScreenBufferInfo failed", d_);
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
        BOOL bOk = ReadConsoleA(d_->hStdin, buf.data(), static_cast<DWORD>( buf.size() ), &read, NULL);
        
        if (!bOk) 
        {
            return -1;
        }
        
        return static_cast<int>( read );
    }

    int ScreenHandle::winWrite( /*int ignored,*/ std::span<const char> cbuf)
    {
        DWORD wrote = 0;
        BOOL bOk = WriteConsoleA(d_->hStdout, cbuf.data(), static_cast<DWORD>(cbuf.size()), &wrote, NULL);
        
        if (!bOk) 
        {
            return -1;
        }
        
        return static_cast<int>( wrote ) ;
    }

    unsigned long winGetLastError() 
    {
        return ::GetLastError();
    }


    

} // wkilocpp namespace