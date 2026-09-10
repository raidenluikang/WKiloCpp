#include <cstdint> // intptr_t
#include <cstdio> // FILE

#include <string>

#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif
#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#endif

// Some code taken from - https://github.com/microsoft/terminal/issues/8820

namespace wkilocpp
{

	struct ScreenSize
	{
		int rows;
		int cols;
	};

	int enableRawMode(void);

	// https://stackoverflow.com/questions/6812224/getting-terminal-size-in-c-for-windows
	ScreenSize getWindowSize();

	// Following code uses the Windows api to read and write to console
	//  instead the C library functions
	//  below stuff works as expected.

	int winRead(int ignored, char* c, int toread);

	int winWrite(int ignored, const char* buf, size_t length);

	//  https://stackoverflow.com/a/47229318/1355145

	/* The original code is public domain -- Will Hartung 4/9/09 */
	/* Modifications, public domain as well, by Antti Haapala, 11/10/17
	   - Switched to getc on 5/23/19 */

	   // if typedef doesn't exist (msvc, blah)
	

	//ptrdiff_t getline(FILE* stream, std::string& line);

	// ==========

	//wchar_t* yk_utf8_to_utf16_null_terminated(const char* str);
	//int yk_io_writefile(const char* fpath, const char* data, size_t len);
	bool writeFileUtf8(std::string_view fpath, std::string_view data);

} // wkilocpp