/*** includes ***/
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS 1
#endif 

#include "terminal_access.hpp"
#include "unicode_space.hpp"

//#include <ctime>
//#include <io.h>
//#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
//#include <cstdarg>

//C++ headers
#include <string_view>
#include <string>
#include <vector>
#include <algorithm>
#include <utility>
#include <functional>
#include <numeric>
#include <format>
#include <chrono>
#include <span>
#include <optional>


/*** defines ***/

namespace wkilocpp
{ 

//#define KILO_VERSION "0.0.1"

constexpr const char* KILO_VERSION = "0.0.1";

//#define KILO_TAB_STOP 8
//#define KILO_QUIT_TIMES 3

constexpr int KILO_TAB_STOP = 8;
constexpr int KILO_QUIT_TIMES = 3;


//#define CTRL_KEY(k) ((k) & 0x1f)
constexpr int CTRL_KEY(const int key) noexcept 
{
    constexpr int mask = 0x1F;
    return key & mask;
}

constexpr bool my_is_space(const char c) noexcept
{
    return unicode::is_space(static_cast<char32_t>(static_cast<unsigned char>(c)));
}

template <typename T> constexpr  bool is_separator(T) = delete; // use only char variant.

constexpr bool is_separator(const char c) noexcept
{
    using namespace std::string_view_literals;

    constexpr std::string_view specials = ",.()+-/*=~%<>[];{}^"sv;

    return my_is_space(c) || (c == '\0') || (specials.find(c) != specials.npos);
}



enum editorKey {
    BACKSPACE = 127,
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN
};

enum editorHighlight {
    HL_NORMAL = 0,
    HL_COMMENT,
    HL_MLCOMMENT,
    HL_KEYWORD1,
    HL_KEYWORD2,
    HL_STRING,
    HL_NUMBER,
    HL_MATCH
};

//#define HL_HIGHLIGHT_NUMBERS (1<<0)
//#define HL_HIGHLIGHT_STRINGS (1<<1)
static constexpr int HL_HIGHLIGHT_NUMBERS = (1 << 0);
static constexpr int HL_HIGHLIGHT_STRINGS = (1 << 1);

/*** data ***/

struct editorSyntax 
{
    std::string_view filetype;
    std::span<const std::string_view > filematch;
    std::span<const std::string_view > keywords;
    std::string_view singleline_comment_start;
    std::string_view multiline_comment_start;
    std::string_view multiline_comment_end;
    int flags;
};

struct editorRow 
{
    int idx;
    int size;
    int rsize;
    char* chars;
    char* render;
    unsigned char* hl;
    bool hl_open_comment;
};

struct editorStatusMessage
{
    using clock_type = std::chrono::high_resolution_clock;
    using timer_type = clock_type::time_point;
    using rep_type = clock_type::duration::rep;

    std::string message;

    timer_type last_time{};

    
    void setMessage(const std::string& message) 
    {
        this->message = message;
        this->last_time = clock_type::now();
    }

    rep_type elapsedMilliseconds() const
    {
        auto now = clock_type::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_time);
        return elapsed.count();
    }
};

struct editorConfig {
    int cx = 0;
    int cy = 0;
    int rx = 0;
    int rowoff = 0;
    int coloff = 0;
    int screenrows = 0;
    int screencols = 0;
    //int numrows = 0;
    std::vector< editorRow > rowList;
    int dirty = 0;
    std::string filename;
    editorStatusMessage statusmsg{};
    std::optional< editorSyntax > syntax ;

    size_t numrows() const noexcept { return rowList.size(); }

    // NOTE: See below item is commented out
    //struct termios orig_termios;
};

struct editorConfig E;

/*** filetypes ***/

constexpr std::string_view C_HL_extensions[] = { ".c", ".h", ".cpp" };
constexpr std::string_view C_HL_keywords[] = {
        "switch", "if", "while", "for", "break", "continue", "return", "else",
        "struct", "union", "typedef", "static", "enum", "class", "case",

        "int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|",
        "void|"
};

constexpr struct editorSyntax HLDB[] = {
        {
                "c",
                C_HL_extensions,
                C_HL_keywords,
                "//", "/*", "*/",
                HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS
        },
};

//#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))
static constexpr int HLDB_ENTRIES = (sizeof(HLDB) / sizeof(HLDB[0]));


//#define write winWrite
//#define read winRead

#ifdef write
#undef write
#endif 

#ifdef read
#undef read
#endif

static int write(int ignored, const char* s, int len) {
    return winWrite(ignored, s, len);
}

static int read(int ignored, char* s, int len) {
    return winRead(ignored, s, len);
}

/*** prototypes ***/

//void editorSetStatusMessage(const char* fmt, ...);
void editorRefreshScreen();
std::string editorPrompt(const std::string_view prompt, void (*callback)(const std::string&, int));

/*** terminal ***/

void die(const char* s) {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(s);
    exit(1);
}


int editorReadKey() {
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) die("read");
    }

    if (c == '\x1b') {
        char seq[3];

        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                if (seq[2] == '~') {
                    switch (seq[1]) {
                    case '1': return HOME_KEY;
                    case '3': return DEL_KEY;
                    case '4': return END_KEY;
                    case '5': return PAGE_UP;
                    case '6': return PAGE_DOWN;
                    case '7': return HOME_KEY;
                    case '8': return END_KEY;
                    }
                }
            }
            else {
                switch (seq[1]) {
                case 'A': return ARROW_UP;
                case 'B': return ARROW_DOWN;
                case 'C': return ARROW_RIGHT;
                case 'D': return ARROW_LEFT;
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
                }
            }
        }
        else if (seq[0] == 'O') {
            switch (seq[1]) {
            case 'H': return HOME_KEY;
            case 'F': return END_KEY;
            }
        }

        return '\x1b';
    }
    else {
        return c;
    }
}

int getCursorPosition(int* rows, int* cols) {
    char buf[32];
    unsigned int i = 0;

    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

    while (i < sizeof(buf) - 1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
        if (buf[i] == 'R') break;
        i++;
    }
    buf[i] = '\0';

    if (buf[0] != '\x1b' || buf[1] != '[') return -1;
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

    return 0;
}


/*** syntax highlighting ***/


void editorUpdateSyntax(editorRow* row) {
    row->hl = (unsigned char*)realloc(row->hl, row->rsize);
    memset(row->hl, HL_NORMAL, row->rsize);

    if (!E.syntax.has_value())
    {
        return;
    }

    std::span<const std::string_view> keywords = E.syntax->keywords;

    const std::string_view scs = E.syntax->singleline_comment_start;
    const std::string_view mcs = E.syntax->multiline_comment_start;
    const std::string_view mce = E.syntax->multiline_comment_end;

    //int scs_len = scs ? (int) strlen(scs) : 0;
    //int mcs_len = mcs ? (int) strlen(mcs) : 0;
    //int mce_len = mce ? (int) strlen(mce) : 0;

    int prev_sep = 1;
    
    int in_string = 0;
    
    bool in_comment = (row->idx > 0 && E.rowList[row->idx - 1].hl_open_comment);

    size_t i = 0;
    while (i < row->rsize) {
        char c = row->render[i];
        unsigned char prev_hl = (i > 0) ? row->hl[i - 1] : HL_NORMAL;

        if (scs.length() > 0 && !in_string && !in_comment) {
            if (!strncmp(&row->render[i], scs.data(), scs.length())) {
                memset(&row->hl[i], HL_COMMENT, row->rsize - i);
                break;
            }
        }

        if (mcs.length() > 0 && mce.length() > 0 && !in_string) {
            if (in_comment) {
                row->hl[i] = HL_MLCOMMENT;
                if (!strncmp(&row->render[i], mce.data(), mce.length())) {
                    memset(&row->hl[i], HL_MLCOMMENT, mce.length());
                    i += mce.length();
                    in_comment = false;
                    prev_sep = 1;
                    continue;
                }
                else {
                    i++;
                    continue;
                }
            }
            else if (!strncmp(&row->render[i], mcs.data(), mcs.length())) {
                memset(&row->hl[i], HL_MLCOMMENT, mcs.length());
                i += mcs.length();
                in_comment = true;
                continue;
            }
        }

        if (E.syntax->flags & HL_HIGHLIGHT_STRINGS) {
            if (in_string) {
                row->hl[i] = HL_STRING;
                if (c == '\\' && i + 1 < row->rsize) {
                    row->hl[i + 1] = HL_STRING;
                    i += 2;
                    continue;
                }
                if (c == in_string) in_string = 0;
                i++;
                prev_sep = 1;
                continue;
            }
            else {
                if (c == '"' || c == '\'') {
                    in_string = c;
                    row->hl[i] = HL_STRING;
                    i++;
                    continue;
                }
            }
        }

        if (E.syntax->flags & HL_HIGHLIGHT_NUMBERS) {
            if ((isdigit(c) && (prev_sep || prev_hl == HL_NUMBER)) ||
                (c == '.' && prev_hl == HL_NUMBER)) {
                row->hl[i] = HL_NUMBER;
                i++;
                prev_sep = 0;
                continue;
            }
        }

        if (prev_sep) {
            int j;
            for (j = 0; j < (int)keywords.size(); j++) {
                int klen = (int)keywords[j].length();
                const bool kw2 = keywords[j].ends_with('|');
                if (kw2) 
                    klen--;

                //TODO: remove strncmp and use string_view.compare.
                if (!strncmp(&row->render[i], keywords[j].data(), klen) &&
                    is_separator(row->render[i + klen])) {
                    memset(&row->hl[i], kw2 ? HL_KEYWORD2 : HL_KEYWORD1, klen);
                    i += klen;
                    break;
                }
            }
            
            if ( j < keywords.size()) 
            {
                prev_sep = 0;
                continue;
            }
        }

        prev_sep = is_separator(c);
        i++;
    }

    const bool changed = (row->hl_open_comment != in_comment);
    row->hl_open_comment = in_comment;
    if (changed && row->idx + 1 < E.numrows())
        editorUpdateSyntax(&E.rowList[row->idx + 1]);
}

int editorSyntaxToColor(int hl) {
    switch (hl) {
    case HL_COMMENT:
    case HL_MLCOMMENT: return 36;
    case HL_KEYWORD1: return 33;
    case HL_KEYWORD2: return 32;
    case HL_STRING: return 35;
    case HL_NUMBER: return 31;
    case HL_MATCH: return 34;
    default: return 37;
    }
}

void editorSelectSyntaxHighlight() 
{
    
    E.syntax = std::nullopt;
    
    if (E.filename.empty())
    {
        return;
    }

    //char* ext = strrchr(E.filename, '.');
    const size_t ext_pos = E.filename.rfind('.');
    std::string_view ext{};
    if (ext_pos != std::string::npos) {
        std::string_view fileview = E.filename;
        ext = fileview.substr(ext_pos);
    }

    for (unsigned int j = 0; j < HLDB_ENTRIES; j++) 
    {
        const editorSyntax s = HLDB[j]; // copy it , because editorSyntax very small structure.

        for (const std::string_view filematch : s.filematch)
        {
            const bool is_ext = filematch.starts_with('.');

            
            //if ((is_ext && ext && !strcmp(ext, filematch.data() ) ) ||
            //    (!is_ext && strstr(E.filename, filematch.data() ) ) ) 
            if ( (is_ext && ext == filematch) ||
                (!is_ext && E.filename.find(filematch) != std::string::npos)
                )
            {

                E.syntax = s;
                for (editorRow& row : E.rowList) {
                    editorUpdateSyntax(&row);
                }
                //for (size_t filerow = 0; filerow < E.numrows(); filerow++) {
                //    editorUpdateSyntax(&E.rowList[filerow]);
               // }

                return;
            }
        }
    }
}

/*** rowList operations ***/

int editorRowCxToRx(editorRow* row, int cx) {
    int rx = 0;
    int j;
    for (j = 0; j < cx; j++) {
        if (row->chars[j] == '\t')
            rx += (KILO_TAB_STOP - 1) - (rx % KILO_TAB_STOP);
        rx++;
    }
    return rx;
}

int editorRowRxToCx(editorRow* row, int rx) {
    int cur_rx = 0;
    int cx;
    for (cx = 0; cx < row->size; cx++) {
        if (row->chars[cx] == '\t')
            cur_rx += (KILO_TAB_STOP - 1) - (cur_rx % KILO_TAB_STOP);
        cur_rx++;

        if (cur_rx > rx) return cx;
    }
    return cx;
}

void editorUpdateRow(editorRow* row) {
    int tabs = 0;
    int j;
    for (j = 0; j < row->size; j++)
        if (row->chars[j] == '\t') tabs++;

    free(row->render);
    row->render = (char*)malloc(row->size + tabs * (KILO_TAB_STOP - 1) + 1);

    int idx = 0;
    for (j = 0; j < row->size; j++) {
        if (row->chars[j] == '\t') {
            row->render[idx++] = ' ';
            while (idx % KILO_TAB_STOP != 0) row->render[idx++] = ' ';
        }
        else {
            row->render[idx++] = row->chars[j];
        }
    }
    row->render[idx] = '\0';
    row->rsize = idx;

    editorUpdateSyntax(row);
}

void editorInsertRow(size_t at, const char* s, size_t len) 
{
    if (at > E.numrows())
    {
        return;
    }

    //E.rowList = (editorRow*)realloc(E.rowList, sizeof(editorRow) * (E.numrows + 1));
    //memmove(&E.rowList[at + 1], &E.rowList[at], sizeof(editorRow) * (E.numrows - at));
    E.rowList.insert(std::next(E.rowList.begin() , at), editorRow{});
    
    for (size_t j = at + 1; j < E.numrows(); j++) {
        E.rowList[j].idx++;
    }

    E.rowList[at].idx = (int)at;

    E.rowList[at].size = (int)len;
    E.rowList[at].chars = (char*)malloc(len + 1);
    memcpy(E.rowList[at].chars, s, len);
    E.rowList[at].chars[len] = '\0';

    E.rowList[at].rsize = 0;
    E.rowList[at].render = NULL;
    E.rowList[at].hl = NULL;
    E.rowList[at].hl_open_comment = false;
    
    editorUpdateRow(&E.rowList[at]);

    //E.numrows++;
    E.dirty++;
}

void editorFreeRow(editorRow* row) {
    free(row->render);
    free(row->chars);
    free(row->hl);
}

void editorDelRow(int at) 
{
    if (at < 0 || at >= E.numrows())
    {
        return;
    }

    editorFreeRow(&E.rowList[at]);

    //memmove(&E.rowList[at], &E.rowList[at + 1], sizeof(editorRow) * (E.numrows - at - 1));
    E.rowList.erase( std::next(E.rowList.begin(), at) );
    
    for (size_t j = at; j < E.numrows(); j++)
    {
        E.rowList[j].idx--;
    }
    //E.numrows--;
    E.dirty++;
}

void editorRowInsertChar(editorRow* row, int at, int c) {
    if (at < 0 || at > row->size) at = row->size;
    row->chars = (char*)realloc(row->chars, row->size + 2);
    memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
    row->size++;
    row->chars[at] = c;
    editorUpdateRow(row);
    E.dirty++;
}

void editorRowAppendString(editorRow* row, char* s, size_t len) {
    row->chars = (char*)realloc(row->chars, row->size + len + 1);
    memcpy(&row->chars[row->size], s, len);
    row->size += (int)len;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
    E.dirty++;
}

void editorRowDelChar(editorRow* row, int at) {
    if (at < 0 || at >= row->size) return;
    memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
    row->size--;
    editorUpdateRow(row);
    E.dirty++;
}

/*** editor operations ***/

void editorInsertChar(int c) {
    if (E.cy == E.numrows()) 
    {
        editorInsertRow(E.numrows(), "", 0);
    }
    editorRowInsertChar(&E.rowList[E.cy], E.cx, c);
    E.cx++;
}

void editorInsertNewline() {
    if (E.cx == 0) {
        editorInsertRow(E.cy, "", 0);
    }
    else {
        editorRow* row = &E.rowList[E.cy];
        editorInsertRow(E.cy + 1, &row->chars[E.cx], row->size - E.cx);
        row = &E.rowList[E.cy];
        row->size = E.cx;
        row->chars[row->size] = '\0';
        editorUpdateRow(row);
    }
    E.cy++;
    E.cx = 0;
}

void editorDelChar() 
{
    if (E.cy == E.numrows())
    {
        return;
    }
    
    if (E.cx == 0 && E.cy == 0)
    {
        return;
    }

    editorRow* row = &E.rowList[E.cy];
    if (E.cx > 0) {
        editorRowDelChar(row, E.cx - 1);
        E.cx--;
    }
    else {
        E.cx = E.rowList[E.cy - 1].size;
        editorRowAppendString(&E.rowList[E.cy - 1], row->chars, row->size);
        editorDelRow(E.cy);
        E.cy--;
    }
}

/*** file i/o ***/

char* editorRowsToString(int* buflen) {
    int totlen = 0;
    
    for (size_t j = 0; j < E.numrows(); j++)
    {
        totlen += E.rowList[j].size + 1;
    }

    *buflen = totlen;

    char* buf = (char*)malloc(totlen);
    char* p = buf;
    
    for (size_t j = 0; j < E.numrows(); j++) 
    {
        memcpy(p, E.rowList[j].chars, E.rowList[j].size);
        p += E.rowList[j].size;
        *p = '\n';
        p++;
    }

    return buf;
}

void editorOpen(const char* filename) {
    //free(E.filename);
    E.filename = filename;
//#if _MSC_VER
//    E.filename = _strdup(filename);
//#else 
//    E.filename = strdup(filename);
//#endif //_MSC_VER

    editorSelectSyntaxHighlight();

    FILE* fp = fopen(filename, "r");
    if (!fp)
    {
        die("fopen");
    }

    char* line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    while ((linelen = getline(&line, &linecap, fp)) != -1) {
        while (linelen > 0 && (line[linelen - 1] == '\n' ||
            line[linelen - 1] == '\r'))
            linelen--;
        editorInsertRow(E.numrows(), line, linelen);
    }
    free(line);
    fclose(fp);
    E.dirty = 0;
}

void editorSave() {
    if (E.filename.empty()) {
        E.filename = editorPrompt("Save as: {} (ESC to cancel)", nullptr);
        if (E.filename.empty()) {
            //editorSetStatusMessage("Save aborted");
            E.statusmsg.setMessage("Save aborted");
            return;
        }
        editorSelectSyntaxHighlight();
    }

    int len;
    char* buf = editorRowsToString(&len);

    if (yk_io_writefile(E.filename.c_str(), buf, len) == 0) {
        free(buf);
        //editorSetStatusMessage("Saved to disk");
        E.statusmsg.setMessage("Saved to disk");
        
        //there no more changes
        E.dirty = 0;
        return;
    }

    free(buf);
    //editorSetStatusMessage("Can't save! I/O error");
    E.statusmsg.setMessage("Can't save! I/O error");
}

/*** find ***/

void editorFindCallback(const std::string& query, int key) {
    static int last_match = -1;
    static int direction = 1;

    static int saved_hl_line;
    static char* saved_hl = NULL;

    if (saved_hl) {
        memcpy(E.rowList[saved_hl_line].hl, saved_hl, E.rowList[saved_hl_line].rsize);
        free(saved_hl);
        saved_hl = NULL;
    }

    if (key == '\r' || key == '\x1b') {
        last_match = -1;
        direction = 1;
        return;
    }
    else if (key == ARROW_RIGHT || key == ARROW_DOWN) {
        direction = 1;
    }
    else if (key == ARROW_LEFT || key == ARROW_UP) {
        direction = -1;
    }
    else {
        last_match = -1;
        direction = 1;
    }

    if (last_match == -1) direction = 1;
    int current = last_match;
    int i;
    for (i = 0; i < E.numrows(); i++) {
        
        current += direction;
        
        if (current == -1) 
            current = (int)E.numrows() - 1;

        else if (current == E.numrows()) 
            current = 0;

        editorRow* row = &E.rowList[current];
        char* match = strstr(row->render, query.c_str());
        if (match) {
            last_match = current;
            E.cy = current;
            E.cx = editorRowRxToCx(row, (int)(match - row->render) );
            E.rowoff = (int)E.numrows();

            saved_hl_line = current;
            saved_hl = (char*)malloc(row->rsize);
            memcpy(saved_hl, row->hl, row->rsize);
            memset(&row->hl[match - row->render], HL_MATCH, /*strlen(query)*/query.length());
            break;
        }
    }
}

void editorFind() {
    int saved_cx = E.cx;
    int saved_cy = E.cy;
    int saved_coloff = E.coloff;
    int saved_rowoff = E.rowoff;

    std::string query = editorPrompt("Search: {} (Use ESC/Arrows/Enter)",
        editorFindCallback);

    if (query.length() > 0) 
    {
        //free(query);
    }
    else 
    {
        E.cx = saved_cx;
        E.cy = saved_cy;
        E.coloff = saved_coloff;
        E.rowoff = saved_rowoff;
    }
}

/*** append buffer ***/
struct abuf 
{
    std::string value;

    void append(const std::string_view sview) 
    {
        value.append(sview.begin(), sview.end());
    }
    
    void append(const char symbol) 
    {
        value.append(1, symbol);
    }
    void append(const char symbol, size_t count)
    {
        value.append(count, symbol);
    }
};

/*** output ***/

void editorScroll() {
    E.rx = 0;
    
    if (E.cy < E.numrows()) 
    {
        E.rx = editorRowCxToRx(&E.rowList[E.cy], E.cx);
    }

    if (E.cy < E.rowoff) {
        E.rowoff = E.cy;
    }
    if (E.cy >= E.rowoff + E.screenrows) {
        E.rowoff = E.cy - E.screenrows + 1;
    }
    if (E.rx < E.coloff) {
        E.coloff = E.rx;
    }
    if (E.rx >= E.coloff + E.screencols) {
        E.coloff = E.rx - E.screencols + 1;
    }
}

void editorDrawRows(struct abuf* ab) {
    using namespace std::string_view_literals;

    int y;
    for (y = 0; y < E.screenrows; y++) {
        int filerow = y + E.rowoff;
        
        if (filerow >= E.numrows() ) 
        {
            if (E.numrows() == 0 && y == E.screenrows / 3) {

                std::string welcome = std::format("Kilo editor -- version {}", KILO_VERSION);
                //char welcome[80];

                //int welcomelen = snprintf(welcome, sizeof(welcome),
                //    "Kilo editor -- version %s", KILO_VERSION);

                //if (welcomelen > E.screencols) 
                //    welcomelen = E.screencols;
                if (welcome.length() > E.screencols) {
                    welcome.erase(welcome.begin() + E.screencols, welcome.end());
                }

                int padding = (E.screencols - (int) welcome.length()) / 2;
                
                if (padding > 0) {
                    //abAppend(ab, "~", 1);
                    ab->append('~');
                    padding--;
                }
                if (padding > 0) {
                    ab->append(' ', static_cast<size_t>(padding));
                }
                //while (padding--) {
                //    //abAppend(ab, " ", 1);
                //    ab->append(' ');
                //}
                
                //abAppend(ab, welcome, welcomelen);
                ab->append(welcome);
            }
            else {
                //abAppend(ab, "~", 1);
                ab->append('~');
            }
        }
        else {
            int len = E.rowList[filerow].rsize - E.coloff;
            if (len < 0) len = 0;
            if (len > E.screencols) len = E.screencols;
            char* c = &E.rowList[filerow].render[E.coloff];
            unsigned char* hl = &E.rowList[filerow].hl[E.coloff];
            int current_color = -1;
            int j;
            for (j = 0; j < len; j++) {
                if (iscntrl(c[j])) {
                    char sym = (c[j] <= 26) ? '@' + c[j] : '?';
                    //abAppend(ab, "\x1b[7m", 4);
                    ab->append("\x1b[7m"sv);

                    //abAppend(ab, &sym, 1);
                    ab->append(sym);


                    //abAppend(ab, "\x1b[m", 3);
                    ab->append("\x1b[m"sv);

                    if (current_color != -1) {
                        //char buf[16];
                        //int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", current_color);
                        //abAppend(ab, buf, clen);
                        //std::string_view buf_view(buf, clen);
                        std::string buf = std::format("\x1b[{}m", current_color);
                        ab->append(buf);
                    }
                }
                else if (hl[j] == HL_NORMAL) {
                    if (current_color != -1) {
                        //abAppend(ab, "\x1b[39m", 5);
                        ab->append("\x1b[39m"sv);
                        current_color = -1;
                    }
                    //abAppend(ab, &c[j], 1);
                    ab->append(c[j]);
                }
                else {
                    int color = editorSyntaxToColor(hl[j]);
                    if (color != current_color) {
                        current_color = color;
                        //char buf[16];
                        //int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", color);
                        //abAppend(ab, buf, clen);
                        //std::string_view buf_view(buf, clen);
                        std::string buf = std::format("\x1b[{}m", color);
                        ab->append(buf);
                    }
                    //abAppend(ab, &c[j], 1);
                    ab->append(c[j]);
                }
            }
            //abAppend(ab, "\x1b[39m", 5);
            ab->append("\x1b[39m"sv);
        }

        //abAppend(ab, "\x1b[K", 3);
        ab->append("\x1b[K"sv);

        //abAppend(ab, "\r\n", 2);
        ab->append("\r\n"sv);
    }
}

void editorDrawStatusBar(struct abuf* ab) {
    using namespace std::literals;
    //abAppend(ab, "\x1b[7m", 4);
    ab->append("\x1b[7m"sv);

    //char status[80], rstatus[80];
    std::string status = std::format( "{:.20} - {} lines {}",
        E.filename.empty() ? "[No Name]"s : E.filename,
        E.numrows(),
        E.dirty ? "(modified)" : "");

    //int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
    //    E.filename ? E.filename : "[No Name]", E.numrows,
    //    E.dirty ? "(modified)" : "");
    //
    std::string rstatus = std::format("{} | {}/{}", E.syntax ? E.syntax->filetype : "no ft", E.cy + 1, E.numrows());

    //int rlen = snprintf(rstatus, sizeof(rstatus), "%s | %d/%d",
    //    E.syntax ? E.syntax->filetype : "no ft", E.cy + 1, E.numrows);
    //
    //if (len > E.screencols) 
    //    len = E.screencols;
    if (status.length() > E.screencols) {
        status.erase(status.begin() + E.screencols, status.end());
    }
    
    //abAppend(ab, status, len);
    ab->append(status);
    
    if (status.length() < E.screencols) {
        if (E.screencols - status.length() >= rstatus.length()) {
            size_t space_count = E.screencols - status.length() - rstatus.length();
            ab->append(' ', space_count);
            ab->append(rstatus);
        }
        else {
            //add only spaces
            size_t space_count = E.screencols - status.length();
            ab->append(' ', space_count);
        }
    }
    //int len = status.length();
    //while (len < E.screencols) {
    //    if (E.screencols - len == rstatus.length()) {
    //        //abAppend(ab, rstatus, rlen);
    //        ab->append(rstatus);
    //        break;
    //    }
    //    else {
    //        //abAppend(ab, " ", 1);
    //        ab->append(' ');
    //        len++;
    //    }
    //}
    //abAppend(ab, "\x1b[m", 3);
    ab->append("\x1b[m"sv);

    //abAppend(ab, "\r\n", 2);
    ab->append("\r\n"sv);
}

void editorDrawMessageBar(struct abuf* ab) 
{
    using namespace std::string_view_literals;

    //abAppend(ab, "\x1b[K", 3);
    ab->append("\x1b[K"sv);

    std::string_view status_view = E.statusmsg.message; // std::string -> std::string_view convertion.

    status_view = status_view.substr(0, static_cast<size_t>(std::max(0, E.screencols)));
    
    if (!status_view.empty()  && E.statusmsg.elapsedMilliseconds() < 5000 )
    {
        //abAppend(ab, E.statusmsg, msglen);
        ab->append(status_view);
    }
}

void editorRefreshScreen() {
    using namespace std::string_view_literals;

    editorScroll();

    struct abuf ab {};// = ABUF_INIT;

    //abAppend(&ab, "\x1b[?25l", 6);
    ab.append("\x1b[?25l"sv);
    //abAppend(&ab, "\x1b[H", 3);
    ab.append("\x1b[H"sv);

    editorDrawRows(&ab);
    editorDrawStatusBar(&ab);
    editorDrawMessageBar(&ab);

    //char buf[32];
    //snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1,
    //    (E.rx - E.coloff) + 1);
    std::string buf = std::format("\x1b[{};{}H", (E.cy - E.rowoff) + 1, (E.rx - E.coloff) + 1);
    
    //abAppend(&ab, buf, (int)strlen(buf));
    ab.append(buf); 

    //abAppend(&ab, "\x1b[?25h", 6);
    ab.append("\x1b[?25h"sv);

    write(STDOUT_FILENO, ab.value.c_str(), (int)ab.value.size());
    //abFree(&ab);
}

//void editorSetStatusMessage(const char* fmt, ...) {
//    va_list ap;
//    va_start(ap, fmt);
//    vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
//    va_end(ap);
//    E.statusmsg_time = time(NULL);
//}

/*** input ***/

std::string editorPrompt(const std::string_view prompt, void (*callback)(const std::string& , int)) {
    constexpr size_t BUF_INITIAL_CAPACITY = 128;
    
    std::string buf;
    buf.reserve(BUF_INITIAL_CAPACITY);

    //size_t bufsize = 128;
    //char* buf = (char*)malloc(bufsize);

    //size_t buflen = 0;
    //buf[0] = '\0';

    while (true) 
    {
        //editorSetStatusMessage(prompt, buf);
        E.statusmsg.setMessage( std::vformat(prompt, std::make_format_args(buf) ) );
        
        editorRefreshScreen();

        const int c = editorReadKey();
        
        if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
            //if (buflen != 0) 
            //    buf[--buflen] = '\0';


            //C++: There removed last element 
            if (!buf.empty()) {
                buf.pop_back();
            }
        }
        else if (c == '\x1b') {
            //editorSetStatusMessage("");
            E.statusmsg.setMessage("");
            
            if (callback) 
                callback(buf, c);

            //free(buf);
            return "";
        }
        else if (c == '\r') {
            if (!buf.empty()) {
                //editorSetStatusMessage("");
                E.statusmsg.setMessage("");
                
                if (callback) 
                    callback(buf, c);

                return buf;
            }
        }
        else if (!iscntrl(c) && c < 128) {
            //if (buflen == bufsize - 1) {
            //    bufsize *= 2;
            //    buf = (char*)realloc(buf, bufsize);
            //}
            //buf[buflen++] = c;
            //buf[buflen] = '\0';

            buf += static_cast<char>(c);
        }

        if (callback) callback(buf, c);
    }
}

void editorMoveCursor(int key) {
    editorRow* row = (E.cy >= E.numrows() || E.cy < 0) ? nullptr: &E.rowList[E.cy];

    switch (key) {
    case ARROW_LEFT:
        if (E.cx != 0) {
            E.cx--;
        }
        else if (E.cy > 0) {
            E.cy--;
            E.cx = E.rowList[E.cy].size;
        }
        break;
    case ARROW_RIGHT:
        if (row && E.cx < row->size) {
            E.cx++;
        }
        else if (row && E.cx == row->size) {
            E.cy++;
            E.cx = 0;
        }
        break;
    case ARROW_UP:
        if (E.cy != 0) {
            E.cy--;
        }
        break;
    case ARROW_DOWN:
        if (E.cy < E.numrows()) {
            E.cy++;
        }
        break;
    }

    
    row = (E.cy >= E.numrows() || E.cy < 0) ? nullptr: &E.rowList[E.cy];

    int rowlen = row ? row->size : 0;
    
    if (E.cx > rowlen) {
        E.cx = rowlen;
    }
}

void editorProcessKeypress() {
    static int quit_times = KILO_QUIT_TIMES;

    int c = editorReadKey();

    switch (c) {
    case '\r':
        editorInsertNewline();
        break;

    case CTRL_KEY('q'):
        if (E.dirty && quit_times > 0) {
            E.statusmsg.setMessage(std::format("WARNING!!! File has unsaved changes. "
                "Press Ctrl-Q {} more times to quit.", quit_times));
            quit_times--;
            return;
        }
        write(STDOUT_FILENO, "\x1b[2J", 4);
        write(STDOUT_FILENO, "\x1b[H", 3);
        exit(0);
        break;

    case CTRL_KEY('s'):
        editorSave();
        break;

    case HOME_KEY:
        E.cx = 0;
        break;

    case END_KEY:
        if (E.cy < E.numrows())
            E.cx = E.rowList[E.cy].size;
        break;

    case CTRL_KEY('f'):
        editorFind();
        break;

    case BACKSPACE:
    case CTRL_KEY('h'):
    case DEL_KEY:
        if (c == DEL_KEY) editorMoveCursor(ARROW_RIGHT);
        editorDelChar();
        break;

    case PAGE_UP:
    case PAGE_DOWN:
    {
        if (c == PAGE_UP) {
            E.cy = E.rowoff;
        }
        else if (c == PAGE_DOWN) {
            E.cy = E.rowoff + E.screenrows - 1;
            if (std::cmp_greater(E.cy,  E.numrows()) ) {
                E.cy = (int)E.numrows();
            }
        }

        int times = E.screenrows;
        while (times--)
            editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
    }
    break;

    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
        editorMoveCursor(c);
        break;

    case CTRL_KEY('l'):
    case '\x1b':
        break;

    default:
        editorInsertChar(c);
        break;
    }

    quit_times = KILO_QUIT_TIMES;
}

/*** init ***/

void initEditor() {
    E.cx = 0;
    E.cy = 0;
    E.rx = 0;
    E.rowoff = 0;
    E.coloff = 0;
    //E.numrows = 0;
    E.rowList.clear();
    E.dirty = 0;
    E.filename = "";
    E.statusmsg.message = "";
    E.statusmsg.last_time = editorStatusMessage::timer_type::min() ;
    E.syntax = std::nullopt;
    
    E.screencols = 0;
    E.screenrows = 0;
    
    if (getWindowSize(&E.screenrows, &E.screencols) == -1)
    {
        die("getWindowSize");
    }
    
    if (E.screencols <= 0 || E.screenrows <= 2) {
        die("getWindowSize incorrect screen cols or rows");
    }

    E.screenrows -= 2;
}

} // wkilocpp namespace

int main(int argc, char* argv[]) 
{
    wkilocpp::enableRawMode();
    wkilocpp::initEditor();
    if (argc >= 2) {
        wkilocpp::editorOpen(argv[1]);
    }

    wkilocpp::E.statusmsg.setMessage("HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find");

    while (true) 
    {
        wkilocpp::editorRefreshScreen();
        wkilocpp::editorProcessKeypress();
    }

    return 0;
}