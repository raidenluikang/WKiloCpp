
#include <cstdio>
#include <cstdlib>

#include <system_error>

#include <filesystem>
#include <fstream>
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
            }

            if (savedConsoleInputMode.has_value())
            {
                SetConsoleMode(hStdin, *savedConsoleInputMode);
            }

            printf("\nBye!\n");
            fflush(stdout);
        }
    };
    
    ScreenHandle::ScreenHandle() 
        : d_(new impl{})
    {}

    ScreenHandle::~ScreenHandle()
    {
        if (d_ != nullptr) 
        {
            d_->disableRawMode();
            delete d_;
        }
    }

        
    ScreenHandle::ScreenHandle(ScreenHandle&& other) noexcept
        : d_(std::exchange(other.d_, nullptr))
    {
    }
    
    ScreenHandle& ScreenHandle::operator = (ScreenHandle&& other) noexcept
    {
        std::swap(d_, other.d_);
        return *this;
    }

    void ScreenHandle::enableRawMode(void)
    {
        if (d_ == nullptr) 
        {
            throw std::invalid_argument("ScreenHandle already moved, do not use it!");
        }

        // Get handles for stdin and stdout
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
    ScreenSize ScreenHandle::getWindowSize() 
    {
        CONSOLE_SCREEN_BUFFER_INFO csbi{};
     
        // https://learn.microsoft.com/en-us/windows/console/getstdhandle
        HANDLE const stdHandle = GetStdHandle(STD_OUTPUT_HANDLE);
        
        if (stdHandle == INVALID_HANDLE_VALUE || stdHandle == NULL) 
        {
            // failed.
            
            // If the function fails, the return value is INVALID_HANDLE_VALUE. To get extended error information, call GetLastError.

            //If an application does not have associated standard handles, 
            // such as a service running on an interactive desktop, and has not redirected them, the return value is NULL.


            throw std::system_error(GetLastError(), std::system_category(), "GetStdHandle(STD_OUTPUT_HANDLE) failed");

            //return ScreenSize{ .rows = -1, .cols = -1 };
        }



        //https://learn.microsoft.com/en-us/windows/console/getconsolescreenbufferinfo
        BOOL const bOk = GetConsoleScreenBufferInfo(stdHandle, &csbi);
        
        if (!bOk) 
        {
            //zero 
            //If the function fails, the return value is zero. To get extended error information, call GetLastError.

            //return ScreenSize{ .rows = -3, .cols = -3 };
            throw std::system_error(GetLastError(), std::system_category(), "GetConsoleScreenBufferInfo failed");
        }
        
        const int col_size = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        const int row_size = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;

        return ScreenSize{ .rows = row_size, .cols = col_size };
    }

    // Following code uses the Windows api to read and write to console
    //  instead the C library functions
    //  below stuff works as expected.

    int ScreenHandle::read(int ignored, char* c, int toread) 
    {
        if (d_ == nullptr || d_->hStdin == INVALID_HANDLE_VALUE || d_->hStdin == NULL) 
        {
            //invalid state
            return -1;
        }

        DWORD read = 0;
        BOOL bOk = ReadConsoleA(d_->hStdin, c, toread, &read, NULL);
        
        if (!bOk) 
        {
            return -1;
        }
        
        return (int)read;
    }

    int ScreenHandle::write(int ignored, const char* buf, size_t length) 
    {
        if (d_ == nullptr || d_->hStdin == INVALID_HANDLE_VALUE || d_->hStdin == NULL) {
            //invalid state
            return -1;
        }

        DWORD wrote = 0;
        BOOL bOk = WriteConsoleA(d_->hStdout, buf, static_cast<DWORD>( length), &wrote, NULL);
        
        if (!bOk) 
        {
            return -1;
        }
        
        return (int)wrote;
    }

    int winGetLastError() 
    {
        return ::GetLastError();
    }


    static std::u8string_view to_u8_view(std::string_view fpath) 
    {
        return std::u8string_view(reinterpret_cast<const char8_t*>(fpath.data()), fpath.size());
    }

    bool writeFileUtf8(std::string_view fpath, std::string_view data) 
    {
        namespace fs = std::filesystem;
    
        // char8_t-конструктор пути — явная гарантия UTF-8 интерпретации,
        // на Windows конвертация в UTF-16 внутри path выполняется автоматически.
        std::u8string_view u8_fpath = to_u8_view(fpath);
        fs::path path(u8_fpath.begin(), u8_fpath.end() );

        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {

            return false;
        }

        file.write(data.data(), static_cast<std::streamsize>(data.size()));


        return file.good();
    }

} // wkilocpp namespace