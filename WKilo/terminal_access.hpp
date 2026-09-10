#include <cstdio>

#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif
#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#endif

// Some code taken from - https://github.com/microsoft/terminal/issues/8820



int enableRawMode(void);

// https://stackoverflow.com/questions/6812224/getting-terminal-size-in-c-for-windows
int getWindowSize(int* rows, int* cols);

// Following code uses the Windows api to read and write to console
//  instead the C library functions
//  below stuff works as expected.

int winRead(int ignored, char* c, int toread);

int winWrite(int ignored, const char* buf, int length);

//  https://stackoverflow.com/a/47229318/1355145

/* The original code is public domain -- Will Hartung 4/9/09 */
/* Modifications, public domain as well, by Antti Haapala, 11/10/17
   - Switched to getc on 5/23/19 */

   // if typedef doesn't exist (msvc, blah)
typedef intptr_t ssize_t;

ssize_t getline(char** lineptr, size_t* n, FILE* stream);

// ==========

wchar_t* yk_utf8_to_utf16_null_terminated(const char* str);
int yk_io_writefile(char* fpath, char* data, int len);