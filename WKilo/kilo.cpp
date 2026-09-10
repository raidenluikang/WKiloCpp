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
//#include <cstdlib>
//#include <cstring>
//#include <cerrno>
//#include <cstdarg>

//C++ headers
#include <iostream>
#include <string_view>
#include <string>
#include <vector>
#include <algorithm>
#include <utility>
#include <array>
#include <functional>
#include <numeric>
#include <format>
#include <chrono>
#include <span>
#include <optional>
#include <charconv>
#include <fstream>
#include <concepts>

/*** defines ***/

namespace wkilocpp
{ 



constexpr const char* KILO_VERSION = "0.0.1";
constexpr int KILO_TAB_STOP = 8;
constexpr int KILO_QUIT_TIMES = 3;



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

constexpr bool my_is_control(const char c) noexcept
{
    return unicode::is_control(static_cast<char32_t>(static_cast<unsigned char>(c)));
}

constexpr bool my_is_digit(const char c) noexcept
{
    return unicode::is_digit(static_cast<char32_t>(static_cast<unsigned char>(c)));
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
constexpr int HL_HIGHLIGHT_NUMBERS = (1 << 0);
constexpr int HL_HIGHLIGHT_STRINGS = (1 << 1);

/*** data ***/
enum class EditorKeyProcessState
{
    do_continue,
    do_exit
};

struct EditorSyntax 
{
    std::string_view filetype;
    std::span<const std::string_view > filematch;
    std::span<const std::string_view > keywords;
    std::string_view singleline_comment_start;
    std::string_view multiline_comment_start;
    std::string_view multiline_comment_end;
    int flags;
};

struct EditorRow 
{
    //int idx;
    std::string chars;
    std::string render;
    std::vector<unsigned char> hl;
    bool hl_open_comment;

    size_t size()const noexcept { return chars.size(); }

    //@TODO: rename it to render_size
    size_t rsize() const noexcept { return render.size(); }

    size_t rowCxToRx(size_t cx) const;

    size_t rowRxToCx(size_t rx) const;

};



struct EditorStatusMessage
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

struct EditorConfig 
{
    int cx;
    int cy;
    int rx;
    int rowoff;
    int coloff;

    ScreenSize screenSize;
    std::vector< EditorRow > rowList;
    int dirty;
    std::string filename;
    EditorStatusMessage statusMessage;
    std::optional< EditorSyntax > syntax ;

    size_t numrows() const noexcept { return rowList.size(); }

    // NOTE: See below item is commented out
    //struct termios orig_termios;

    EditorConfig();
    ~EditorConfig();

    std::string rowsToString() const;
};


/*** filetypes ***/

constexpr std::string_view C_HL_extensions[] = { ".c", ".h", ".cpp" };
constexpr std::string_view C_HL_keywords[] = {
        "switch", "if", "while", "for", "break", "continue", "return", "else",
        "struct", "union", "typedef", "static", "enum", "class", "case",

        "int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|",
        "void|"
};

constexpr  std::array<EditorSyntax, 1> HLDB = {
        {
                "c",
                C_HL_extensions,
                C_HL_keywords,
                "//", "/*", "*/",
                HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS
        },
};



/*** prototypes ***/

struct abuf; //forward declaration.



/*** terminal ***/
template <typename C >  concept TerminalCallback = std::is_invocable_v<C, const std::string&, int>;

struct TerminalEditor
{
    EditorConfig editor_;

    int quit_times_ = KILO_QUIT_TIMES;

    int last_match_ = -1;
    int direction_ = 1;

    size_t saved_hl_line_ = 0;
    std::optional<std::vector<unsigned char>> saved_hl_ ;

    ScreenHandle screenHandle_;

    TerminalEditor();
    ~TerminalEditor();

    void die(const char* s);
    
    int writeOutput(const std::string_view s) 
    {
        return screenHandle_.winWrite(STDOUT_FILENO, s.data(), s.length());
    }

    int readInput(char* s, int len) 
    {
        return screenHandle_.winRead(STDIN_FILENO, s, len);
    }



    int readKey();

    ScreenSize getCursorPosition();

    void updateSyntax(size_t row_index);

    static int syntaxToColor(const int hl);

    void selectSyntaxHighlight();

    
    void updateRow(size_t row_index);

    void insertRow(size_t at, std::string_view c_view);

    void deleteRow(size_t at);

    void rowInsertChar(size_t row_index,   size_t at, char c);

    void rowAppendString(size_t row_index, const std::string_view c_view);

    void rowDeleteChar(size_t row_index, size_t at);

    void insertChar(char c);

    void insertNewline();

    void deleteChar();

    

    void openFile(const std::string& filename);

    void saveToFile();

    void findCallback(const std::string& query, int key);

    void find();

    void scroll();

    void drawRows(abuf* ab);

    void drawStatusBar(abuf* ab);

    void drawMessageBar(abuf* ab);

    void refreshScreen();

    template <TerminalCallback Callback>
    std::string prompt(const std::string_view prompt_fmt, Callback callback);

    std::string prompt(const std::string_view promt_fmt) //without callback version
    {
        auto cb = [](const std::string&, int) {}; // empty callback
        return this->prompt(promt_fmt, cb);
    }

    void moveCursor(int key);

    EditorKeyProcessState processKeypress();


};

void TerminalEditor::die(const char* s) 
{
    
    using namespace std::string_view_literals;

    writeOutput("\x1b[2J"sv);
    writeOutput("\x1b[H"sv);
    
    throw std::system_error( winGetLastError(), std::system_category(), s);
}


int TerminalEditor::readKey() 
{
    int nread = 0;
    char c = 0;

    while ((nread = readInput(&c, 1)) != 1) 
    {
        if (nread == -1)
        {
            die("read");
        }
    }
    
    if (c == '\x1b') 
    {
        char seq[3]{};

        if (readInput( &seq[0], 1) != 1) return '\x1b';
        if (readInput( &seq[1], 1) != 1) return '\x1b';

        if (seq[0] == '[') 
        {
            if (seq[1] >= '0' && seq[1] <= '9') 
            {
                if (readInput(&seq[2], 1) != 1) return '\x1b';

                if (seq[2] == '~') 
                {
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
                switch (seq[1]) 
                {
                case 'A': return ARROW_UP;
                case 'B': return ARROW_DOWN;
                case 'C': return ARROW_RIGHT;
                case 'D': return ARROW_LEFT;
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
                }
            }
        }
        else if (seq[0] == 'O') 
        {
            switch (seq[1]) {
            case 'H': return HOME_KEY;
            case 'F': return END_KEY;
            }
        }

        return '\x1b';
    }
    else 
    {
        return c;
    }
}

[[maybe_unused]]
ScreenSize TerminalEditor::getCursorPosition() 
{
    ScreenSize result{ .rows = -1, .cols = -1 };

    char buf[32]{};

    unsigned int i = 0;
    
    using namespace std::string_view_literals;

    if (writeOutput("\x1b[6n"sv) != 4) 
        return result;

    while (i < sizeof(buf) - 1) 
    {
        if (readInput(&buf[i], 1) != 1) 
            break;

        if (buf[i] == 'R') break;
        i++;
    }
    
    buf[i] = '\0';

    if (buf[0] != '\x1b' || buf[1] != '[') 
        return result;

    //if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) 
    //    return -1;
    {
        const char* start_buf = buf + 2;
        const char* end_buf = buf + i;

        //1. winRead rows
        const auto [ptr_row, ec_row] = std::from_chars(start_buf, end_buf, result.rows);
        if (ec_row != std::errc{}) {
            return result;
        }

        //2. winRead cols
        if (!(ptr_row != end_buf && *ptr_row == ';')) {
            // ';' separator not found
            return result;
        }
        start_buf = ptr_row + 1;

        const auto [ptr_col, ec_col] = std::from_chars(start_buf, end_buf, result.cols);
        if (ec_col != std::errc{}) {
            return result;
        }
    }

    return result;
}


/*** syntax highlighting ***/


void TerminalEditor::updateSyntax(size_t row_index) 
{
    if (row_index >= editor_.rowList.size()) {
        return;
    }

    auto& row = editor_.rowList[row_index];

    row.hl.assign(row.rsize(), HL_NORMAL);

    if (!editor_.syntax.has_value())
    {
        return;
    }

    std::span<const std::string_view> keywords = editor_.syntax->keywords;

    const std::string_view scs = editor_.syntax->singleline_comment_start;
    const std::string_view mcs = editor_.syntax->multiline_comment_start;
    const std::string_view mce = editor_.syntax->multiline_comment_end;


    int prev_sep = 1;
    
    int in_string = 0;
    
    bool in_comment = (row_index > 0 && editor_.rowList[row_index - 1].hl_open_comment);

    size_t i = 0;
    while (i < row.rsize()) 
    {
        char c = row.render[i];
        
        unsigned char prev_hl = (i > 0) ? row.hl[i - 1] : HL_NORMAL;

        if (scs.length() > 0 && !in_string && !in_comment) {
            
            std::string_view render_ith = std::string_view(row.render).substr(i);
            
            if (render_ith.starts_with(scs) )
            {
                std::fill(row.hl.begin() + i, row.hl.end(), HL_COMMENT);
                
                break;
            }
        }

        if (mcs.length() > 0 && mce.length() > 0 && !in_string) {
            std::string_view render_ith = std::string_view(row.render).substr(i);
            
            if (in_comment) 
            {
                row.hl[i] = HL_MLCOMMENT;
                
                
                if (render_ith.starts_with(mce) )
                {
                    
                    std::fill_n(row.hl.begin() + i, mce.length(), HL_MLCOMMENT);
                    i += mce.length();
                    in_comment = false;
                    prev_sep = 1;
                    continue;
                }
                else 
                {
                    i++;
                    continue;
                }
            }
            else  if ( render_ith.starts_with(mcs) )
            {
                
                std::fill_n(row.hl.begin() + i, mcs.length(), HL_MLCOMMENT);
                i += mcs.length();
                in_comment = true;
                continue;
            }
        }

        if (editor_.syntax->flags & HL_HIGHLIGHT_STRINGS) {
            if (in_string) {
                row.hl[i] = HL_STRING;
                
                if (c == '\\' && i + 1 < row.rsize() ) 
                {
                    row.hl[i + 1] = HL_STRING;
                    i += 2;
                    continue;
                }
                
                if (c == in_string) 
                    in_string = 0;

                i++;
                prev_sep = 1;
                continue;
            }
            else {
                if (c == '"' || c == '\'') 
                {
                    in_string = c;
                    row.hl[i] = HL_STRING;
                    i++;
                
                    continue;
                }
            }
        }

        if (editor_.syntax->flags & HL_HIGHLIGHT_NUMBERS) 
        {
            //@TODO: replace isdigit to constexpr my_is_digit variant.
            if ((my_is_digit(c) && (prev_sep || prev_hl == HL_NUMBER)) ||
                (c == '.' && prev_hl == HL_NUMBER)) 
            {
                row.hl[i] = HL_NUMBER;
                i++;
                prev_sep = 0;
                continue;
            }
        }

        if (prev_sep) 
        {
            int j;
            for (j = 0; j < (int)keywords.size(); j++) {
                
                std::string_view keyword = keywords[j];

                const bool kw2 = keyword.ends_with('|');
                if (kw2)
                    keyword.remove_suffix(1);

                
                std::string_view render_ith = std::string_view(row.render).substr(i);

                if ( render_ith == keyword || 
                        (
                            render_ith.starts_with(keyword) &&
                            is_separator( render_ith[keyword.length() ] ) 
                        ) 
                    ) 
                {
                    unsigned char fill_value = kw2 ? HL_KEYWORD2 : HL_KEYWORD1;
                    
                    std::fill_n(row.hl.begin() + i, keyword.length(), fill_value);
                    i += keyword.length();
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

    const bool changed = (row.hl_open_comment != in_comment);

    row.hl_open_comment = in_comment;

    if (changed) 
    {
        //automatic checks inside index overflow.
        updateSyntax(row_index + 1);
    }
}

int TerminalEditor::syntaxToColor(const int hl) 
{
    switch (hl) 
    {
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

void TerminalEditor::selectSyntaxHighlight() 
{
    
    editor_.syntax = std::nullopt;
    
    if (editor_.filename.empty())
    {
        return;
    }

    
    const size_t ext_pos = editor_.filename.rfind('.');
    
    std::string_view ext{};
    
    if (ext_pos != std::string::npos) 
    {
        std::string_view fileview = editor_.filename;
        ext = fileview.substr(ext_pos);
    }

    for (const EditorSyntax& s : HLDB) 
    {
        for (const std::string_view filematch : s.filematch)
        {
            const bool is_ext = filematch.starts_with('.');

            
            if ( (is_ext && ext == filematch) ||
                (!is_ext && editor_.filename.find(filematch) != std::string::npos)
                )
            {

                editor_.syntax = s;
                
                for (size_t index = 0; index != editor_.rowList.size(); index++) 
                {
                    updateSyntax(index);
                }

                return;
            }
        }
    }
}

/*** rowList operations ***/

size_t EditorRow::rowCxToRx(size_t cx) const
{
    return std::accumulate(chars.begin(), chars.begin() + std::min(cx, chars.size()), size_t{0},
        [](size_t rx, char c) 
        {
            return c == '\t' ? (rx / KILO_TAB_STOP + 1) * KILO_TAB_STOP : rx + 1;
        });



    //size_t rx = 0;
    //
    //for (size_t j = 0; j < cx; j++) {
    //    if (chars[j] == '\t')
    //        rx += (KILO_TAB_STOP - 1) - (rx % KILO_TAB_STOP);
    //    rx++;
    //}
    //return rx;
}

size_t EditorRow::rowRxToCx(size_t rx) const
{
    size_t cur_rx = 0;
    
    for (size_t cx = 0; cx < this->size(); cx++) 
    {
        const char c = chars[cx];

        cur_rx =  (c == '\t') ? (cur_rx / KILO_TAB_STOP + 1) * KILO_TAB_STOP : cur_rx + 1;

        //if (chars[cx] == '\t')
        //    cur_rx += (KILO_TAB_STOP - 1) - (cur_rx % KILO_TAB_STOP);
    
        //cur_rx++;

        if (cur_rx > rx)
        {
            return cx;
        }
    }

    return this->size();
}


void TerminalEditor::updateRow(size_t row_index) 
{
    if (row_index >= editor_.rowList.size()) {
        return;
    }

    auto& row = editor_.rowList[row_index];
    
    const ptrdiff_t tabs = std::count(row.chars.cbegin(), row.chars.cend(), '\t');

    //@NOTE: this is a hack, for full destroy allocated memory of row->render.
    if (row.render.capacity() > editor_.screenSize.cols) {
        std::string{}.swap(row.render);
    }
    else {
        //a simple clear
        row.render.clear();
    }

    
    for (const char c : row.chars) 
    {
        if (c == '\t')
        {
            row.render += ' ';
            while (row.render.size() % KILO_TAB_STOP != 0)
            {
                row.render += ' ';
            }
        }
        else 
        {
            row.render  += c;
        }
    }

    updateSyntax(row_index);
}

void TerminalEditor::insertRow(size_t at, std::string_view c_view ) 
{
    if (at > editor_.numrows())
    {
        return;
    }

    const auto makeRow = [](const std::string_view cv) 
    {
        EditorRow newRow{}; // all fields automatic initialized.
        newRow.chars.assign(cv);
        
        return newRow;//C++17 constructor elision works
    };
    

    editor_.rowList.insert(std::next(editor_.rowList.begin(), at), makeRow(c_view) );
    
    updateRow(at);

    editor_.dirty++;
}


void TerminalEditor::deleteRow(size_t at) 
{
    if (at >= editor_.numrows())
    {
        return;
    }

    editor_.rowList.erase( std::next(editor_.rowList.begin(), at) );

    editor_.dirty++;
}

void TerminalEditor::rowInsertChar(size_t row_index, size_t at, char c) 
{
    if (row_index >= editor_.rowList.size())
    {
        return;
    }
    
    auto& row = editor_.rowList[row_index];
    
    if (at > row.size())
    {
        at = row.size();
    }

    row.chars.insert(at, 1, c);
    updateRow(row_index);
    editor_.dirty++;
}

void TerminalEditor::rowAppendString(size_t row_index, const std::string_view c_view) 
{
    if (row_index >= editor_.rowList.size()) 
    {
        return;
    }
    auto& row = editor_.rowList[row_index];
    row.chars.append(c_view.data(), c_view.size());
    updateRow(row_index);
    editor_.dirty++;
}

void TerminalEditor::rowDeleteChar(size_t row_index, size_t at) 
{
    if (row_index >= editor_.rowList.size())
    {
        return;
    }
    auto& row = editor_.rowList[row_index];
    if ( at >= row.size() ) 
    {
        return;
    }
    
    row.chars.erase(std::next(row.chars.begin(), at));
    
    //@NOTE: some optimization for memory usage
    if (row.chars.capacity() / 2 >= row.chars.size()) {
        row.chars.shrink_to_fit();
    }

    updateRow( row_index);
    editor_.dirty++;
}

/*** editor operations ***/

void TerminalEditor::insertChar(char c) 
{
    if (editor_.cy == editor_.numrows()) 
    {
        insertRow(editor_.numrows(), "");
    }
    
    size_t row_index = editor_.cy;
    rowInsertChar(row_index, editor_.cx, c);
    editor_.cx++;
}

void TerminalEditor::insertNewline() {
    if (editor_.cx == 0) 
    {
        insertRow(editor_.cy, "");
    }
    else  
    {
        const EditorRow& cur_row = editor_.rowList[editor_.cy];
        

        //TODO: think about when editor_.cx == row->size() case.
        if (std::cmp_less(editor_.cx, cur_row.size())) 
        {
            std::string_view chars_view = cur_row.chars;
            insertRow(editor_.cy + 1,  chars_view.substr(editor_.cx) );

            editor_.rowList[editor_.cy].chars.resize(editor_.cx);
            

            size_t row_index = editor_.cy;
            updateRow(row_index);
        }
        else if (std::cmp_equal(editor_.cx, cur_row.size()))
        {
            insertRow(editor_.cy + 1, "");//empty string will be added

            size_t row_index = editor_.cy;
            updateRow(row_index);
        }
    }
    editor_.cy++;
    editor_.cx = 0;
}

void TerminalEditor::deleteChar()
{
    if (editor_.cx == 0 && editor_.cy == 0)
    {
        return;
    }
    
    if (editor_.rowList.empty()) {
        return;//nothing to be deleted.
    }
    

    if (editor_.cy == editor_.numrows())
    {
        editor_.cy--;
        editor_.cx = static_cast<int> (editor_.rowList[editor_.cy].size());
        // в самом деле ничего не добавляется и удаляется. просто курсор перемещается в конце передыдущий строку.
        return;
    }
        
    
    if (editor_.cx > 0) 
    {
        size_t row_index = editor_.cy;
        rowDeleteChar(row_index, editor_.cx - 1);
        editor_.cx--;
    }
    else if (editor_.cy > 0)
    {
        const EditorRow& cur_row = editor_.rowList[editor_.cy];

        size_t prev_row_index = editor_.cy - 1;
        editor_.cx = static_cast< int > ( editor_.rowList[prev_row_index].size() ) ;
        rowAppendString(prev_row_index,  cur_row.chars);
        deleteRow(editor_.cy);
        editor_.cy--;
    }
}

/*** file i/o ***/

std::string EditorConfig::rowsToString() const 
{
    const size_t totlen = std::accumulate(rowList.cbegin(), rowList.cend(), size_t{ 0 },
        [](const size_t sum, const EditorRow & row) { return sum + row.size() + 1; });

    std::string buf;
    buf.reserve(totlen);
    
    for (const auto& row : rowList) 
    {
        buf += row.chars;
        buf += '\n';
    }

    return buf;
}

void TerminalEditor::openFile(const std::string& filename) {
    
    editor_.filename = filename;

    selectSyntaxHighlight();

    std::ifstream fp(filename);
    
    if (!fp.is_open())
    {
        die("fopen");
    }

    std::string line;
    
    while ( std::getline(fp, line) )
    {
        
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
        {
            line.pop_back();
        }

        insertRow(editor_.numrows(), line );
    }
    
    
    editor_.dirty = 0;
}

void TerminalEditor::saveToFile() {
    
    if (editor_.filename.empty()) 
    {
        editor_.filename = this->prompt("Save as: {} (ESC to cancel)");
    
        if (editor_.filename.empty()) 
        {
            editor_.statusMessage.setMessage("Save aborted");
            return;
        }
        
        selectSyntaxHighlight();
    }

    
    const std::string buf = editor_.rowsToString();

    
    const bool bOk = writeFileUtf8(editor_.filename, buf);
    
    if (!bOk) 
    {
        editor_.statusMessage.setMessage("Can't save! I/O error");
        return;
    }
    

    editor_.statusMessage.setMessage("Saved to disk");

    editor_.dirty = 0;
    
}

/*** find ***/

void TerminalEditor::findCallback(const std::string& query, int key) {

    if (saved_hl_.has_value()) 
    {
        if (saved_hl_line_ < editor_.rowList.size()) 
        {
            EditorRow& row = editor_.rowList[saved_hl_line_];
            row.hl.swap(*saved_hl_);
        }
    
        saved_hl_ = std::nullopt;
        saved_hl_line_ = 0;
    }

    if (key == '\r' || key == '\x1b') {
        last_match_ = -1;
        direction_ = 1;
        return;
    }
    else if (key == ARROW_RIGHT || key == ARROW_DOWN) {
        direction_ = 1;
    }
    else if (key == ARROW_LEFT || key == ARROW_UP) {
        direction_ = -1;
    }
    else {
        last_match_ = -1;
        direction_ = 1;
    }

    if (last_match_ == -1) direction_ = 1;
    int current = last_match_;
    //int i;
    for (size_t i = 0; i < editor_.numrows(); i++) {
        
        current += direction_;
        
        if (current == -1) 
            current = (int)editor_.numrows() - 1;

        else if (current == editor_.numrows()) 
            current = 0;

        EditorRow& row = editor_.rowList[current];
        
        size_t match_pos = row.render.find(query);

        if (match_pos != std::string::npos) 
        {
            last_match_ = current;
            editor_.cy = current;
            editor_.cx = (int)row.rowRxToCx(match_pos);
            editor_.rowoff = (int)editor_.numrows();

            saved_hl_line_ = current;
            
            saved_hl_ = row.hl; // copy it and save.

            
            std::fill_n(row.hl.begin() + match_pos, query.length(), HL_MATCH);
            break;
        }
    }
}

void TerminalEditor::find() {
    int saved_cx = editor_.cx;
    int saved_cy = editor_.cy;
    int saved_coloff = editor_.coloff;
    int saved_rowoff = editor_.rowoff;

    std::string query = this->prompt("Search: {} (Use ESC/Arrows/Enter)",
        std::bind_front(&TerminalEditor::findCallback, this) );

    if (query.length() > 0) 
    {
        
    }
    else 
    {
        editor_.cx = saved_cx;
        editor_.cy = saved_cy;
        editor_.coloff = saved_coloff;
        editor_.rowoff = saved_rowoff;
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

void TerminalEditor::scroll() {
    editor_.rx = 0;
    
    if (editor_.cy < editor_.numrows()) 
    {
        editor_.rx = (int)editor_.rowList[editor_.cy].rowCxToRx(editor_.cx);
    }

    if (editor_.cy < editor_.rowoff) 
    {
        editor_.rowoff = editor_.cy;
    }
    
    if (editor_.cy >= editor_.rowoff + editor_.screenSize.rows) 
    {
        editor_.rowoff = editor_.cy - editor_.screenSize.rows + 1;
    }
    
    if (editor_.rx < editor_.coloff) 
    {
        editor_.coloff = editor_.rx;
    }

    if (editor_.rx >= editor_.coloff + editor_.screenSize.cols) 
    {
        editor_.coloff = editor_.rx - editor_.screenSize.cols + 1;
    }
}

void TerminalEditor::drawRows(struct abuf* ab) {
    using namespace std::string_view_literals;

    int y;
    for (y = 0; y < editor_.screenSize.rows; y++) {
        int filerow = y + editor_.rowoff;
        
        if (filerow >= editor_.numrows() ) 
        {
            if (editor_.numrows() == 0 && y == editor_.screenSize.rows / 3) {

                std::string welcome = std::format("Kilo editor -- version {}", KILO_VERSION);
                
                if (welcome.length() > editor_.screenSize.cols) 
                {
                    welcome.erase(std::next(welcome.begin(), editor_.screenSize.cols), welcome.end());
                }

                int padding = (editor_.screenSize.cols - (int) welcome.length()) / 2;
                
                if (padding > 0) {
        
                    ab->append('~');
                    padding--;
                }
                if (padding > 0) {
                    ab->append(' ', static_cast<size_t>(padding));
                }
                ab->append(welcome);
            }
            else {
        
                ab->append('~');
            }
        }
        else {
            int len = static_cast<int>( editor_.rowList[filerow].rsize() ) - editor_.coloff;
            
            if (len < 0) 
                len = 0;

            if (len > editor_.screenSize.cols) 
                len = editor_.screenSize.cols;

            //@NOTE: this condition is required, otherwice may access empty vector.
            if (len > 0) {
                char* c = &editor_.rowList[filerow].render[editor_.coloff];

                unsigned char* hl = &editor_.rowList[filerow].hl[editor_.coloff];

                int current_color = -1;
                int j;
                for (j = 0; j < len; j++) {
                    if (my_is_control(c[j])) {
                        char sym = (c[j] <= 26) ? '@' + c[j] : '?';
        
                        ab->append("\x1b[7m"sv);

        
                        ab->append(sym);


        
                        ab->append("\x1b[m"sv);

                        if (current_color != -1) {
                            std::string buf = std::format("\x1b[{}m", current_color);
                            ab->append(buf);
                        }
                    }
                    else if (hl[j] == HL_NORMAL) {
                        if (current_color != -1) {
        
                            ab->append("\x1b[39m"sv);
                            current_color = -1;
                        }
        
                        ab->append(c[j]);
                    }
                    else {
                        int color = syntaxToColor(hl[j]);
                        if (color != current_color) {
                            current_color = color;
                            std::string buf = std::format("\x1b[{}m", color);
                            ab->append(buf);
                        }
        
                        ab->append(c[j]);
                    }
                }
            } // end if len > 0
        
            ab->append("\x1b[39m"sv);
        }

        
        ab->append("\x1b[K"sv);

        
        ab->append("\r\n"sv);
    }
}

void TerminalEditor::drawStatusBar(struct abuf* ab) {
    using namespace std::literals;
    ab->append("\x1b[7m"sv);

    std::string status = std::format( "{:.20} - {} lines {}",
        editor_.filename.empty() ? "[No Name]"s : editor_.filename,
        editor_.numrows(),
        editor_.dirty ? "(modified)" : "");

    std::string rstatus = std::format("{} | {}/{}", editor_.syntax ? editor_.syntax->filetype : "no ft", editor_.cy + 1, editor_.numrows());

    if (status.length() > editor_.screenSize.cols) {
        status.erase(status.begin() + editor_.screenSize.cols, status.end());
    }
    
    ab->append(status);
    
    if (status.length() < editor_.screenSize.cols) {
        if (editor_.screenSize.cols - status.length() >= rstatus.length()) {
            size_t space_count = editor_.screenSize.cols - status.length() - rstatus.length();
            ab->append(' ', space_count);
            ab->append(rstatus);
        }
        else {
            //add only spaces
            size_t space_count = editor_.screenSize.cols - status.length();
            ab->append(' ', space_count);
        }
    }
    
    ab->append("\x1b[m"sv);

    
    ab->append("\r\n"sv);
}

void TerminalEditor::drawMessageBar(struct abuf* ab)
{
    using namespace std::string_view_literals;

    
    ab->append("\x1b[K"sv);

    std::string_view status_view = editor_.statusMessage.message; 

    status_view = status_view.substr(0, static_cast<size_t>(std::max(0, editor_.screenSize.cols)));
    
    if (!status_view.empty()  && editor_.statusMessage.elapsedMilliseconds() < 5000 )
    {
        ab->append(status_view);
    }
}

void TerminalEditor::refreshScreen() {
    using namespace std::string_view_literals;

    scroll();

    struct abuf ab {};

    ab.append("\x1b[?25l"sv);
    ab.append("\x1b[H"sv);

    drawRows(&ab);
    drawStatusBar(&ab);
    drawMessageBar(&ab);

    std::string buf = std::format("\x1b[{};{}H", (editor_.cy - editor_.rowoff) + 1, (editor_.rx - editor_.coloff) + 1);
    
    ab.append(buf); 

    ab.append("\x1b[?25h"sv);

    writeOutput(ab.value);
}


/*** input ***/

template <TerminalCallback Callback>
std::string TerminalEditor::prompt(const std::string_view prompt_fmt, Callback callback) 
{
    constexpr size_t BUF_INITIAL_CAPACITY = 128;
    
    std::string buf;
    buf.reserve(BUF_INITIAL_CAPACITY);

    
    while (true) 
    {
        editor_.statusMessage.setMessage( std::vformat(prompt_fmt, std::make_format_args(buf) ) );
        
        refreshScreen();

        const int c = readKey();
        
        if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) 
        {
            //if (buflen != 0) 
            //    buf[--buflen] = '\0';


            //C++: There removed last element 
            if (!buf.empty()) {
                buf.pop_back();
            }
        }
        else if (c == '\x1b') {
            editor_.statusMessage.setMessage("");
            
            callback(buf, c);

            return "";
        }
        else if (c == '\r') 
        {
            
            if (!buf.empty()) 
            {
               editor_.statusMessage.setMessage("");
                
               callback(buf, c);

                return buf;
            }
        }
        else if (!my_is_control(c) && c < 128) {
            buf += static_cast<char>(c);
        }

        callback(buf, c);
    }
}

void TerminalEditor::moveCursor(int key) {
    EditorRow* row = (editor_.cy >= editor_.numrows() || editor_.cy < 0) ? nullptr: &editor_.rowList[editor_.cy];

    switch (key) {
    case ARROW_LEFT:
        if (editor_.cx != 0) {
            editor_.cx--;
        }
        else if (editor_.cy > 0) {
            editor_.cy--;
            editor_.cx = static_cast< int > ( editor_.rowList[editor_.cy].size() ) ;
        }
        break;
    case ARROW_RIGHT:
        if (row && editor_.cx < row->size()) 
        {
            editor_.cx++;
        }
        else if (row && editor_.cx == row->size()) 
        {
            editor_.cy++;
            editor_.cx = 0;
        }
        break;
    case ARROW_UP:
        if (editor_.cy != 0) {
            editor_.cy--;
        }
        break;
    case ARROW_DOWN:
        if (editor_.cy < editor_.numrows()) {
            editor_.cy++;
        }
        break;
    }

    
    row = (editor_.cy >= editor_.numrows() || editor_.cy < 0) ? nullptr: &editor_.rowList[editor_.cy];

    int rowlen = row ? (int) row->size() : 0;
    
    if (editor_.cx > rowlen) {
        editor_.cx = rowlen;
    }
}

EditorKeyProcessState TerminalEditor::processKeypress() {

    using namespace std::string_view_literals;

    int c = readKey();

    switch (c) {
    case '\r':
        insertNewline();
        break;

    case CTRL_KEY('q'):
        if (editor_.dirty && quit_times_ > 0) 
        {
            editor_.statusMessage.setMessage(std::format("WARNING!!! File has unsaved changes. "
                "Press Ctrl-Q {} more times to quit.", quit_times_));
            quit_times_--;
            return EditorKeyProcessState::do_continue;
        }
        
        writeOutput("\x1b[2J"sv);
        writeOutput("\x1b[H"sv);

        return EditorKeyProcessState::do_exit;
        
        break;

    case CTRL_KEY('s'):
        saveToFile();
        break;

    case HOME_KEY:
        editor_.cx = 0;
        break;

    case END_KEY:
        if (editor_.cy < editor_.numrows())
        {
            editor_.cx = static_cast< int > ( editor_.rowList[editor_.cy].size() ) ;
        }
        break;

    case CTRL_KEY('f'):
        find();
        break;

    case BACKSPACE:
    case CTRL_KEY('h'):
    case DEL_KEY:
        if (c == DEL_KEY) 
        {
            moveCursor(ARROW_RIGHT);
        }
        
        deleteChar();
        break;

    case PAGE_UP:
    case PAGE_DOWN:
    {
        if (c == PAGE_UP) {
            editor_.cy = editor_.rowoff;
        }
        else if (c == PAGE_DOWN) {
            editor_.cy = editor_.rowoff + editor_.screenSize.rows - 1;
            if (std::cmp_greater(editor_.cy,  editor_.numrows()) ) {
                editor_.cy = (int)editor_.numrows();
            }
        }

        int times = editor_.screenSize.rows;
        while (times--) 
        {
            moveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
        }
    }
    break;

    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
        moveCursor(c);
        break;

    case CTRL_KEY('l'):
    case '\x1b':
        break;

    default:
        insertChar(c);
        break;
    }

    quit_times_ = KILO_QUIT_TIMES;

    return EditorKeyProcessState::do_continue;
}


/*** init ***/
EditorConfig::EditorConfig() 
{
    cx = 0;
    cy = 0;
    rx = 0;
    rowoff = 0;
    coloff = 0;
    dirty = 0;
    statusMessage.last_time = EditorStatusMessage::timer_type::min();
    screenSize = ScreenSize{};
}

EditorConfig::~EditorConfig()
{

}

TerminalEditor::TerminalEditor()
    : editor_{} // initialize editor_
{
    //1. enableRaw mode
    screenHandle_.enableRawMode();

    //2. screen size initialize inside TerminalEditor.
    editor_.screenSize = ScreenHandle::getWindowSize();

    if (editor_.screenSize.cols <= 0 || editor_.screenSize.rows <= 0)
    {
        die("getWindowSize");
    }

    if (editor_.screenSize.rows <= 2)
    {
        die("ScreenSize rows very small!");
    }

    editor_.screenSize.rows -= 2;
}



TerminalEditor::~TerminalEditor()
{
}

} // wkilocpp namespace

int main(int argc, char* argv[]) 
{
    try 
    {
        
        wkilocpp::TerminalEditor terminalEditor{};

        if (argc >= 2) 
        {
            terminalEditor.openFile(argv[1]);
        }

        terminalEditor.editor_.statusMessage.setMessage("HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find");

        using State = wkilocpp::EditorKeyProcessState;

        while (true)
        {
            terminalEditor.refreshScreen();
            
            State state = terminalEditor.processKeypress();
            
            switch (state) {
            case State::do_continue:
                //continue
                break;
            case State::do_exit:
                //exit
                return 0;
            //for future case other states...
            }
        }
    }
    
    catch (const std::exception& exception) 
    {
        std::cerr << exception.what() << std::endl;
    }
    catch (...) {
        std::cerr << "Unexpected unknown exception." << std::system_category().message( wkilocpp::winGetLastError() ) << std::endl;;
    }

    return 0;
}