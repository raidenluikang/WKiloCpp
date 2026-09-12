/*** includes ***/

#include "terminal_access.hpp"
#include "unicode_space.hpp"
#include "kilo_common.hpp"

#if APP_HAS_EXCEPTIONS
#include <system_error>
#endif //!APP_HAS_EXCEPTIONS

#include <cassert>

//C++ headers
#include <filesystem>
#include <fstream>
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
#include <concepts>
#include <ranges>

#pragma warning(disable : 4820) //padding bytes no interesting.
#pragma warning(disable: 5045) //  /Qspectre not interesting in current moment

namespace wkilocpp
{ 

constexpr std::string_view KILO_VERSION = "0.0.1";
constexpr int KILO_TAB_STOP = 8;
constexpr int KILO_QUIT_TIMES = 3;

constexpr char ESCAPE_SYMBOL = '\x1b';


[[nodiscard]]
constexpr int CTRL_KEY(const int key) noexcept 
{
    constexpr int mask = 0x1F;
    return key & mask;
}

[[nodiscard]]
constexpr bool my_is_space(const char c) noexcept
{
    return unicode::is_space(static_cast<char32_t>(static_cast<unsigned char>(c)));
}

template <typename T> constexpr  bool is_separator(T) = delete; // use only char variant.

[[nodiscard]]
constexpr bool is_separator(const char c) noexcept
{
    using namespace std::literals::string_view_literals;

    constexpr std::string_view specials = ",.()+-/*=~%<>[];{}^"sv;

    return my_is_space(c) || (c == '\0') || (specials.find(c) != specials.npos);
}

[[nodiscard]]
constexpr bool my_is_control(const char c) noexcept
{
    return unicode::is_control(static_cast<char32_t>(static_cast<unsigned char>(c)));
}

[[nodiscard]]
constexpr bool my_is_control(const int c ) noexcept
{
    return unicode::is_control(static_cast<char32_t>(static_cast<unsigned int>(c)));
}
template <typename T> constexpr bool my_is_control(T ) noexcept = delete;//other variants should be error.


[[nodiscard]]
constexpr bool my_is_digit(const char c) noexcept
{
    return unicode::is_digit(static_cast<char32_t>(static_cast<unsigned char>(c)));
}


template <typename Container, typename SizeType >
void erase_at(Container& container, SizeType at) {
    container.erase(std::next(container.cbegin(), static_cast<typename Container::difference_type >( at ) ) );
}

template <typename Container, typename SizeType>
void erase_at_end(Container& container, SizeType at) {
    container.erase(std::next(container.cbegin(), static_cast<typename Container::difference_type>(at)), container.cend());
}

template <typename Container, typename T>
auto insert_at(Container& container, size_t at, T&& value) {
    return container.insert(std::next(container.cbegin(), static_cast<typename Container::difference_type>(at)), std::forward<T>(value));
}


//@TODO: made it enum class.
enum EditorKey 
{
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

enum class EditorHighlight : unsigned char
{
    HL_NORMAL = 0,
    HL_COMMENT,
    HL_MLCOMMENT,
    HL_KEYWORD1,
    HL_KEYWORD2,
    HL_STRING,
    HL_NUMBER,
    HL_MATCH
};

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
    std::string chars;
    std::string render;
    std::vector<enum EditorHighlight> hl;
    bool hl_open_comment;

    [[nodiscard]]
    size_t size() const noexcept { return chars.size(); }

    [[nodiscard]]
    size_t render_size() const noexcept { return render.size(); }

    [[nodiscard]]
    size_t rowCxToRx(size_t cx) const noexcept;

    [[nodiscard]]
    size_t rowRxToCx(size_t rx) const noexcept;

};



struct EditorStatusMessage
{
    using clock_type = std::chrono::high_resolution_clock;
    using timer_type = clock_type::time_point;
    using rep_type = clock_type::duration::rep;

    std::string message;

    timer_type last_time{};

    
    void setMessage(std::string messageArg) 
    {
        this->message = std::move(messageArg);
        this->last_time = clock_type::now();
    }

    [[nodiscard]]
    rep_type elapsedMilliseconds() const noexcept
    {
        auto now = clock_type::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_time);
        return elapsed.count();
    }
};

struct EditorConfig 
{
    size_t cx;
    size_t cy;
    size_t rx;
    size_t rowoff;
    size_t coloff;

    ScreenSize screenSize;
    std::vector< EditorRow > rowList;
    int dirty;
    std::string filename;
    EditorStatusMessage statusMessage;
    std::optional< EditorSyntax > syntax ;

    [[nodiscard]]
    size_t numrows() const noexcept { return rowList.size(); }

    // NOTE: See below item is commented out
    //struct termios orig_termios;

    EditorConfig();
    ~EditorConfig();

    [[nodiscard]]
    bool writeToFile(std::ofstream& file) const;
};


/*** filetypes ***/

constexpr std::string_view C_HL_extensions[] = { ".c", ".h", ".cpp" };
constexpr std::string_view C_HL_keywords[] = {
        "switch", "if", "while", "for", "break", "continue", "return", "else",
        "struct", "union", "typedef", "static", "enum", "class", "case",
        "const",

        "int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|",
        "void|", "bool|", "short|"
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

/*** terminal ***/
template <typename C >  
concept TerminalCallback = std::invocable<C, const std::string&, int>;

template <typename C>
concept MessageCallback = std::invocable<C, const std::string&> and std::convertible_to<std::invoke_result_t<C, const std::string&>, std::string>;

class TerminalEditor
{
    EditorConfig editor_;

    int quit_times_ = KILO_QUIT_TIMES;

    int last_match_ = -1;
    int direction_ = 1;

    size_t saved_hl_line_ = 0;
    std::optional< std::vector< enum EditorHighlight> > saved_hl_ ;

    ScreenHandle screenHandle_;

    struct abuf refresh_abuf_;

public:
    explicit TerminalEditor(int argc, char* argv[]);
    ~TerminalEditor();

    TerminalEditor(const TerminalEditor&) = delete;
    TerminalEditor& operator = (const TerminalEditor&) = delete;

    int run();

private:
    [[noreturn]]
    void die(const char* s);
    
    int writeOutput(const std::string_view cbuf) 
    {
        return screenHandle_.winWrite( /*/STDOUT_FILENO,*/ cbuf);
    }

    int readInput(const std::span<char> buf) 
    {
        return screenHandle_.winRead(/*STDIN_FILENO,*/ buf);
    }


    [[nodiscard]]
    int readKey();

    ScreenSize getCursorPosition();

    //return changed or not
    [[nodiscard]]
    bool updateSyntaxImpl(size_t row_index);

    //updated [row_index .. end) until changed.
    void updateSyntax(size_t row_index);


    static constexpr int syntaxToColor(const  enum EditorHighlight hl) noexcept;

    bool selectSyntaxHighlight();

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

    void drawRows();

    void drawStatusBar();

    void drawMessageBar();

    void refreshScreen();

    template <TerminalCallback Callback, MessageCallback CallbackForMsg >
    [[nodiscard]]
    std::string prompt(Callback callback, CallbackForMsg msgCb);


    void moveCursor(int key);
    void moveCursorPageUp(size_t step);
    void moveCursorPageDown(size_t step);

    [[nodiscard]]
    EditorKeyProcessState processKeypress();

    void resetTerminalState() noexcept;
};


void TerminalEditor::resetTerminalState() noexcept
{
    using namespace std::string_view_literals;

    writeOutput("\x1b[2J"sv);
    writeOutput("\x1b[H"sv);

    screenHandle_.disableRawMode();
}

[[noreturn]]
void TerminalEditor::die(const char* s) 
{

#if (APP_HAS_EXCEPTIONS)
    throw std::system_error(static_cast< int >( winGetLastError() ), std::system_category(), s);
#else 
    // Исключения отключены. Явно сбрасываем состояние терминала перед падением
    resetTerminalState();

    // Выводим ошибку в std::cerr, так как буфер std::cout при abort() не сбросится
    std::cerr << "Fatal error: " << s << " (Error code: " << winGetLastError() << ")\n" << std::endl;

    std::abort();
#endif

}


[[nodiscard]]
int TerminalEditor::readKey() 
{
    int nread = 0;
    

    char buf_c[1] = {};

    while ((nread = readInput( std::span<char, 1>( buf_c ) ) ) != 1) 
    {
        if (nread == -1)
        {
            die("read");
        }
    }
    
    

    const char c = buf_c[ 0 ];

    if (c != ESCAPE_SYMBOL)
    {
        return c;
    }

    //--- There c == ESCAPE_SYMBOL ---
    
    char seq[3]{};
        
    const std::span<char, 3> seq_span(seq);

    if (readInput( seq_span.subspan<0, 1>() ) != 1) 
        return ESCAPE_SYMBOL;

    if (readInput( seq_span.subspan<1, 1>() ) != 1) 
        return ESCAPE_SYMBOL;


    if (seq[0] == '[') 
    {
        if (seq[1] >= '0' && seq[1] <= '9') 
        {
            if (readInput( seq_span.subspan<2, 1>() ) != 1) 
                return ESCAPE_SYMBOL;

            if (seq[2] == '~') 
            {
                switch (seq[1]) 
                {
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

    return ESCAPE_SYMBOL;
    
}

[[maybe_unused]]
ScreenSize TerminalEditor::getCursorPosition() 
{
    ScreenSize result{ .rows = 0, .cols = 0 };

    char buf[32]{};
    
    const std::span<char, sizeof(buf)> buf_span(buf);

    unsigned int i = 0;
    
    using namespace std::string_view_literals;

    if (writeOutput("\x1b[6n"sv) != 4) 
        return result;

    while (i < sizeof(buf) - 1) 
    {
        if (readInput( buf_span.subspan(i, 1) ) != 1)
            break;

        if (buf[i] == 'R') break;
        i++;
    }
    
    buf[i] = '\0';

    if (buf[0] != ESCAPE_SYMBOL || buf[1] != '[') 
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
[[nodiscard]]
bool TerminalEditor::updateSyntaxImpl(size_t row_index) 
{
    if (row_index >= editor_.rowList.size()) 
    {
        return false;
    }

    auto& row = editor_.rowList[row_index];

    row.hl.assign(row.render_size(), EditorHighlight::HL_NORMAL);

    if (!editor_.syntax.has_value())
    {
        return false;
    }

    const std::span<EditorHighlight> hl_view = row.hl;

    const std::span<const std::string_view> keywords = editor_.syntax->keywords;

    const std::string_view scs = editor_.syntax->singleline_comment_start;
    const std::string_view mcs = editor_.syntax->multiline_comment_start;
    const std::string_view mce = editor_.syntax->multiline_comment_end;


    bool prev_is_sep = true; 
    
    std::optional<char> in_string = std::nullopt;
    
    bool in_comment = (row_index > 0 && editor_.rowList[row_index - 1].hl_open_comment);

    size_t i = 0;
    while (i < row.render_size()) 
    {
        const char c = row.render[i];
        
        const enum EditorHighlight prev_hl = (i > 0) ? row.hl[i - 1] : EditorHighlight::HL_NORMAL;

        if ( !scs.empty() && !in_string && !in_comment) {
            
            const auto render_ith = std::string_view(row.render).substr(i);
            
            if (render_ith.starts_with(scs) )
            {
                std::ranges::fill(hl_view.subspan(i), EditorHighlight::HL_COMMENT);
                //std::fill(row.hl.begin() + i, row.hl.end(), EditorHighlight::HL_COMMENT);
                
                break;
            }
        }

        
        if (mcs.length() > 0 && mce.length() > 0 && !in_string) 
        {
            const auto render_ith = std::string_view(row.render).substr(i);
            
            if (in_comment) 
            {
                row.hl[i] = EditorHighlight::HL_MLCOMMENT;
                
                if (render_ith.starts_with(mce) )
                {
                    
                    //std::fill_n(row.hl.begin() + i, mce.length(), EditorHighlight::HL_MLCOMMENT);
                    std::ranges::fill(hl_view.subspan(i, mce.length()), EditorHighlight::HL_MLCOMMENT);

                    i += mce.length();
                    in_comment = false;
                    prev_is_sep = true;
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
                
                //std::fill_n(row.hl.begin() + i, mcs.length(), EditorHighlight::HL_MLCOMMENT);
                std::ranges::fill(hl_view.subspan(i, mcs.length()), EditorHighlight::HL_MLCOMMENT);
                i += mcs.length();
                in_comment = true;
                continue;
            }
        }

        if (editor_.syntax->flags & HL_HIGHLIGHT_STRINGS) {
            
            if (in_string.has_value()) 
            {
                row.hl[i] = EditorHighlight::HL_STRING;
                
                if (c == '\\' && i + 1 < row.render_size() ) 
                {
                    row.hl[i + 1] = EditorHighlight::HL_STRING;
                    i += 2;
                    continue;
                }
                
                if (c == in_string)
                {
                    in_string = std::nullopt;
                }

                i++;
                prev_is_sep = true;
                continue;
            }
            else {
                if (c == '"' || c == '\'') 
                {
                    in_string = c;
                    row.hl[i] = EditorHighlight::HL_STRING;
                    i++;
                
                    continue;
                }
            }
        }

        if (editor_.syntax->flags & HL_HIGHLIGHT_NUMBERS) 
        {
            //@TODO: replace isdigit to constexpr my_is_digit variant.
            if ((my_is_digit(c) && (prev_is_sep || prev_hl == EditorHighlight::HL_NUMBER)) ||
                (c == '.' && prev_hl == EditorHighlight::HL_NUMBER))
            {
                row.hl[i] = EditorHighlight::HL_NUMBER;
                i++;
                prev_is_sep = false;
                continue;
            }
        }

        if (prev_is_sep) 
        {
            const auto render_ith = std::string_view(row.render).substr(i);

            const auto keyword_match = [render_ith](std::string_view keyword) -> bool
            {
                    const bool kw2 = keyword.ends_with('|');
                    if (kw2)
                    {
                        keyword.remove_suffix(1);
                    }

                    if (!render_ith.starts_with(keyword)) {
                        return false;
                    }

                    return (render_ith.length() == keyword.length() ||
                            is_separator(render_ith[keyword.length()])
                            );
            };

            const auto iter_kw = std::ranges::find_if(keywords, keyword_match);

            if (iter_kw != keywords.end()) 
            {
                std::string_view keyword = *iter_kw;
            
                const bool kw2 = keyword.ends_with('|');
                
                if (kw2) 
                    keyword.remove_suffix(1);

                const enum EditorHighlight fill_value = kw2 ? EditorHighlight::HL_KEYWORD2 : EditorHighlight::HL_KEYWORD1;

                //std::fill_n(row.hl.begin() + i, keyword.length(), fill_value);
                std::ranges::fill(hl_view.subspan(i, keyword.length()), fill_value);

                i += keyword.length();

                prev_is_sep = false;

                continue;

            }
        }

        prev_is_sep = is_separator(c);
        i++;
    }

    const bool changed = (row.hl_open_comment != in_comment);

    row.hl_open_comment = in_comment;

    return changed;
}

void TerminalEditor::updateSyntax(size_t row_index)
{
    //@NOTE: there need < operator, because row_index may be greater than rowList.size initially.
    for (size_t idx = row_index; idx < editor_.rowList.size(); ++idx) 
    {
        const bool changed = updateSyntaxImpl(idx);
        if (!changed)
            break;
    }
}

constexpr int TerminalEditor::syntaxToColor(const enum EditorHighlight hl) noexcept
{
    switch (hl) 
    {
        using enum EditorHighlight;
    case HL_COMMENT:
    case HL_MLCOMMENT: return 36;
    case HL_KEYWORD1: return 33;
    case HL_KEYWORD2: return 32;
    case HL_STRING: return 35;
    case HL_NUMBER: return 31;
    case HL_MATCH: return 34;
    
    case HL_NORMAL:
    default: 
        return 37;
    }
}

bool TerminalEditor::selectSyntaxHighlight() 
{
    
    editor_.syntax = std::nullopt;
    
    if (editor_.filename.empty())
    {
        return false;
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

            const bool is_matched = (is_ext && ext == filematch) ||
                (!is_ext && editor_.filename.find(filematch) != std::string::npos);
            
            if ( is_matched )
            {
                editor_.syntax = s;
                
                for (size_t index = 0; index != editor_.rowList.size(); index++) 
                {
                    // impl does not recursive call themself. ignore return value.
                    [[maybe_unused]] 
                    const bool changed = updateSyntaxImpl(index);

                }

                return true;
            }
        }
    }
    return false;
}

/*** rowList operations ***/
[[nodiscard]]
size_t EditorRow::rowCxToRx(size_t cx) const noexcept
{
    const auto size = static_cast<std::string::difference_type> (std::min(cx, chars.size()));

    return std::accumulate(chars.begin(), std::next(chars.begin(),  size ), size_t{0},
        [](size_t rx, char c) 
        {
            return c == '\t' ? (rx / KILO_TAB_STOP + 1) * KILO_TAB_STOP : rx + 1;
        });
}

[[nodiscard]]
size_t EditorRow::rowRxToCx(size_t rx) const noexcept
{
    size_t cur_rx = 0;
    size_t cx = 0;
    for (const char c : chars)
    {
        cur_rx =  (c == '\t') ? (cur_rx / KILO_TAB_STOP + 1) * KILO_TAB_STOP : cur_rx + 1;

        if (cur_rx > rx)
        {
            return cx;
        }
        cx++;
    }

    return cx;
}


void TerminalEditor::updateRow(size_t row_index) 
{
    if (row_index >= editor_.rowList.size()) {
        return;
    }

    auto& row = editor_.rowList[row_index];
    
    //const ptrdiff_t tabs = std::count(row.chars.cbegin(), row.chars.cend(), '\t');

    
    if (std::cmp_greater(row.render.capacity(),  editor_.screenSize.cols) ) 
    {
        //@NOTE: this is a hack, for full destroy allocated memory of row->render.
        std::string{}.swap(row.render);
    }
    else 
    {
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
    
    //const auto position = std::next(editor_.rowList.cbegin(), static_cast<ptrdiff_t>( at ) );
    insert_at(editor_.rowList, at, makeRow(c_view) );
    
    updateRow(at);

    editor_.dirty++;
}


void TerminalEditor::deleteRow(size_t at) 
{
    if (at >= editor_.numrows())
    {
        return;
    }

    //editor_.rowList.erase( std::next(editor_.rowList.cbegin(), static_cast<ptrdiff_t>(at) ) );
    erase_at(editor_.rowList, at);

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
    
    //row.chars.erase(std::next(row.chars.cbegin(), static_cast< ptrdiff_t>( at) ) );
    erase_at(row.chars, at);
    
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
    if ( std::cmp_equal(editor_.cy, editor_.numrows()) ) 
    {
        insertRow(editor_.numrows(), "");
    }
    
    size_t row_index = static_cast<size_t>( editor_.cy ) ;
    rowInsertChar(row_index, static_cast<size_t>(editor_.cx), c);
    editor_.cx++;
}

void TerminalEditor::insertNewline() {
    if (editor_.cx == 0) 
    {
        insertRow(static_cast< size_t >( editor_.cy ), "");
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
            

            size_t row_index = static_cast< size_t >( editor_.cy ) ;
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
    

    if ( std::cmp_equal( editor_.cy, editor_.numrows() ) )
    {
        assert(editor_.cy > 0);

        editor_.cy--;
        editor_.cx = editor_.rowList[editor_.cy].size();
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
        editor_.cx =  editor_.rowList[prev_row_index].size();
        rowAppendString(prev_row_index,  cur_row.chars);
        deleteRow(editor_.cy);
        editor_.cy--;
    }
}

/*** file i/o ***/
[[nodiscard]]
bool EditorConfig::writeToFile(std::ofstream& file) const
{
    if (!file.is_open()) 
    {
        return false;
    }
    
    constexpr char newline[ 1 ] = { '\n' };

    for (const EditorRow& row : rowList) 
    {
        file.write(row.chars.data(), static_cast<std::streamsize>(row.chars.size()) );
    
        if (!file.good()) 
        {
            return false;
        }
        
        file.write(newline, 1);
    }

    return file.good();
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

    
static std::u8string_view to_u8_view(std::string_view fpath)
{
    return std::u8string_view(reinterpret_cast<const char8_t*>(fpath.data()), fpath.size());
}

void TerminalEditor::saveToFile() 
{
    bool const noFileName = editor_.filename.empty();
    if (noFileName) 
    {
        auto mainCb = [](const std::string&, int) {}; //do nothing.
        
        auto msgCb = [](const std::string& buf) 
        {
            return std::format("Save as: {} (ESC to cancel)", buf);
        };

        editor_.filename = this->prompt(mainCb, msgCb);
    
        if (editor_.filename.empty()) 
        {
            editor_.statusMessage.setMessage("Save aborted");
            return;
        }
        
        selectSyntaxHighlight();
    }

    
    {
        namespace fs = std::filesystem;

        std::u8string_view u8_fpath = to_u8_view(editor_.filename);
        
        //For Windows filesystem path guaranteed UTF8 -> UTF8 conversation when use char8_t.
        const fs::path path(u8_fpath.begin(), u8_fpath.end());

        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        
        if (!file.is_open()) 
        {
            if (noFileName) {
                //previously do not open file
                editor_.filename = "";//clear it.
            }
            editor_.statusMessage.setMessage("Can't open file for write! I/O error");
            return ;
        }
        
        const bool ok = editor_.writeToFile(file);
        
        if (!ok) 
        {
            editor_.statusMessage.setMessage("Can't save! I/O error");
            return;
        }
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

    if (key == '\r' || key == ESCAPE_SYMBOL) {
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

    if (last_match_ == -1) 
        direction_ = 1;

    int current = last_match_;
    
    for (size_t i = 0; i < editor_.numrows(); i++) {
        
        current += direction_;
        
        if (current < 0) 
            current = (int)editor_.numrows() - 1;
        else if (std::cmp_equal(current, editor_.numrows() ) ) 
            current = 0;
        
        assert(current >= 0);

        const auto current_sz = static_cast<size_t>(current);

        EditorRow& row = editor_.rowList[current_sz];
        
        const std::span hl_view = row.hl; // CDAT std::span<T>

        size_t match_pos = row.render.find(query);

        if (match_pos != std::string::npos) 
        {
            last_match_ = current;
            editor_.cy =  current_sz;
            editor_.cx = row.rowRxToCx(match_pos);
            editor_.rowoff = editor_.numrows();

            saved_hl_line_ = current_sz;
            
            saved_hl_ = row.hl; // copy it and save.

            
            //std::fill_n(row.hl.begin() + match_pos, query.length(), EditorHighlight::HL_MATCH);
            std::ranges::fill(hl_view.subspan(match_pos, query.length()), EditorHighlight::HL_MATCH);
            break;
        }
    }
}

void TerminalEditor::find() 
{
    const size_t saved_cx = editor_.cx;
    const size_t saved_cy = editor_.cy;
    const size_t saved_coloff = editor_.coloff;
    const size_t saved_rowoff = editor_.rowoff;

    auto mainCb = std::bind_front(&TerminalEditor::findCallback, this);

    auto msgCb = [](const std::string& buf) 
        { 
            return std::format("Search: {} (Use ESC/Arrows/Enter)", buf); 
        };

    std::string query = this->prompt(mainCb, msgCb);

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



/*** output ***/

void TerminalEditor::scroll() {
    editor_.rx = 0;
    
    if (editor_.cy < editor_.numrows()) 
    {
        editor_.rx = editor_.rowList[editor_.cy].rowCxToRx(editor_.cx);
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

void TerminalEditor::drawRows() 
{
    using namespace std::string_view_literals;

    const size_t rows = static_cast<size_t>( editor_.screenSize.rows);
    const size_t cols = static_cast<size_t>(editor_.screenSize.cols);

    for (size_t y = 0; y < rows; y++) 
    {
        const size_t filerow = y + editor_.rowoff;
        
        if (filerow >= editor_.numrows() ) 
        {
            if (editor_.numrows() == 0 && y == rows / 3) {

                std::string welcome = std::format("Kilo editor -- version {}", KILO_VERSION);
                
                if (welcome.length() > cols) 
                {
                    //welcome.erase(std::next(welcome.cbegin(), static_cast<ptrdiff_t>(cols)), welcome.end());
                    erase_at_end(welcome, cols);
                }
                
                assert(welcome.length() <= cols);

                //There welcome.length() <= cols guarantted.
                size_t padding = (cols -  welcome.length()) / 2;
                
                if (padding > 0) 
                {
        
                    refresh_abuf_.append('~');
                    padding--;
                }
                
                if (padding > 0) 
                {
                    refresh_abuf_.append(' ', padding);
                }
                
                refresh_abuf_.append(welcome);
            }
            else 
            {
        
                refresh_abuf_.append('~');
            }
        }
        else 
        {
            auto& rw = editor_.rowList[filerow];
            
            const size_t len = [&]()->size_t
                {
                    if (rw.render_size() <= editor_.coloff)
                        return 0;
                    
                    if (std::cmp_greater(rw.render_size() - editor_.coloff, editor_.screenSize.cols))
                        return static_cast<size_t>(editor_.screenSize.cols);

                    return rw.render_size() - editor_.coloff;
                }();
            
            //@NOTE: this condition is required, otherwice may access empty vector.
            if (len > 0) 
            {
                std::string_view cr = std::string_view(rw.render).substr(editor_.coloff);
                
                std::span< enum EditorHighlight> hl = std::span(rw.hl).subspan(editor_.coloff);
                
                std::optional<int> current_color = std::nullopt;
                
                for (size_t j = 0; j != len; j++) 
                {
                    if ( my_is_control( cr[ j ] ) ) 
                    {
                        const char sym = (cr[ j ] <= 26) ? '@' + cr[ j ] : '?';
        
                        refresh_abuf_.append("\x1b[7m"sv);

        
                        refresh_abuf_.append(sym);


                        refresh_abuf_.append("\x1b[m"sv);

                        if (current_color.has_value()) 
                        {
                            std::string buf = std::format("\x1b[{}m", *current_color);
                            refresh_abuf_.append(buf);
                        }
                    }
                    else if (hl[j] == EditorHighlight::HL_NORMAL) 
                    {
                        if (current_color.has_value()) 
                        {
                            refresh_abuf_.append("\x1b[39m"sv);
                            current_color = std::nullopt;
                        }
        
                        refresh_abuf_.append(cr[j]);
                    }
                    else 
                    {
                        const int color = syntaxToColor(hl[j]);

                        if (color != current_color) 
                        {
                            current_color = color;
                        
                            std::string buf = std::format("\x1b[{}m", color);
                            refresh_abuf_.append(buf);
                        }
        
                        refresh_abuf_.append(cr[j]);
                    }
                }
            } // end if len > 0
        
            refresh_abuf_.append("\x1b[39m"sv);
        }

        
        refresh_abuf_.append("\x1b[K"sv);

        
        refresh_abuf_.append("\r\n"sv);
    }
}

void TerminalEditor::drawStatusBar() {
    using namespace std::literals;
    
    refresh_abuf_.append("\x1b[7m"sv);

    std::string status = std::format( "{:.20} - {} lines {}",
        editor_.filename.empty() ? "[No Name]"s : editor_.filename,
        editor_.numrows(),
        editor_.dirty ? "(modified)" : "");


    std::string rstatus = std::format("{:.10} | {}/{}", editor_.syntax.has_value() ? editor_.syntax->filetype : "no ft"sv,
        editor_.cy + 1, editor_.numrows());

    if (status.length() > editor_.screenSize.cols) {
        //status.erase(status.begin() + editor_.screenSize.cols, status.end());
        erase_at_end(status, editor_.screenSize.cols);
    }
    
    refresh_abuf_.append(status);
    
    if (status.length() < editor_.screenSize.cols) {
        if (editor_.screenSize.cols - status.length() >= rstatus.length()) {
            size_t space_count = editor_.screenSize.cols - status.length() - rstatus.length();
            refresh_abuf_.append(' ', space_count);
            refresh_abuf_.append(rstatus);
        }
        else {
            //add only spaces
            size_t space_count = editor_.screenSize.cols - status.length();
            refresh_abuf_.append(' ', space_count);
        }
    }
    
    

    refresh_abuf_.append("\x1b[m"sv);

    
    refresh_abuf_.append("\r\n"sv);
}

void TerminalEditor::drawMessageBar()
{
    using namespace std::string_view_literals;

    
    refresh_abuf_.append("\x1b[K"sv);

    std::string_view status_view = editor_.statusMessage.message; 

    status_view = status_view.substr(0, editor_.screenSize.cols);
    
    if (!status_view.empty()  && editor_.statusMessage.elapsedMilliseconds() < 5000 )
    {
        refresh_abuf_.append(status_view);
    }
}

void TerminalEditor::refreshScreen() {
    using namespace std::string_view_literals;

    scroll();

    /* NOTE: 
        refresh screen always prints about rows x cols  symbols, so why always re-create a memory
        for this abuf string.
        Use previous allocated memory in std::string.
    */

    refresh_abuf_.value.clear(); // clear it. But allocated memory do not deallocated.

    refresh_abuf_.append("\x1b[?25l"sv);
    refresh_abuf_.append("\x1b[H"sv);

    drawRows();
    drawStatusBar();
    drawMessageBar();

    std::string buf = std::format("\x1b[{};{}H", (editor_.cy - editor_.rowoff) + 1, (editor_.rx - editor_.coloff) + 1);
    
    refresh_abuf_.append(buf);

    
    
    

    refresh_abuf_.append("\x1b[?25h"sv);

    writeOutput(refresh_abuf_.value);

    refresh_abuf_.value.clear();//there also clear.
}


/*** input ***/

template <TerminalCallback Callback, MessageCallback CallbackForMsg>
[[nodiscard]]
std::string TerminalEditor::prompt(Callback callback, CallbackForMsg msgCb) 
{
    constexpr size_t BUF_INITIAL_CAPACITY = 128;
    
    std::string buf;
    buf.reserve(BUF_INITIAL_CAPACITY);

    
    while (true) 
    {
        editor_.statusMessage.setMessage( msgCb(buf) );
        
        refreshScreen();

        const int c = readKey();
        
        if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) 
        {
            //C++: There removed last element 
            if (!buf.empty()) {
                buf.pop_back();
            }
        }
        else if (c == ESCAPE_SYMBOL) {
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
        else if (!my_is_control(c) && c < 128) 
        {
            buf += static_cast<char>(c);
        }

        callback(buf, c);
    }
}

void TerminalEditor::moveCursorPageUp(size_t step)
{
    for (size_t i = 0; i < step; i++) {
        moveCursor(ARROW_UP);
    }
}

void TerminalEditor::moveCursorPageDown(size_t step)
{
    for (size_t i = 0; i < step; i++) {
        moveCursor(ARROW_DOWN);
    }
}

void TerminalEditor::moveCursor(int key) 
{
    switch (key) 
    {
    case ARROW_LEFT:
        if (editor_.cx > 0) 
        {
            editor_.cx--;
        }
        else if (editor_.cy > 0) 
        {
            editor_.cy--;
            editor_.cx =  editor_.rowList[editor_.cy].size() ;
        }
        break;
    case ARROW_RIGHT:
        if (std::cmp_less(editor_.cy , editor_.numrows() ) )
        {
            size_t row_size = editor_.rowList[editor_.cy].size();

            if ( std::cmp_less( editor_.cx , row_size) )
            {
                editor_.cx++;
            }
            else if ( std::cmp_equal(editor_.cx , row_size) )
            {
                editor_.cy++;
                editor_.cx = 0;
            }
        }
        else 
        {
            //do nothing
        }
        break;
    case ARROW_UP:
        if (editor_.cy  > 0) {
            editor_.cy--;
        }
        break;
    case ARROW_DOWN:
        if (editor_.cy < editor_.numrows()) {
            editor_.cy++;
        }
        break;
    }

    //accurate cx 
    if ( std::cmp_less(editor_.cy,  editor_.numrows()) ) 
    {
        editor_.cx = std::min(editor_.cx, editor_.rowList[editor_.cy].size());
    }
    else 
    {
        editor_.cx = 0;
    }
}

[[nodiscard]]
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
            editor_.cx =  editor_.rowList[editor_.cy].size();
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
    {
        editor_.cy = editor_.rowoff;
        moveCursorPageUp(editor_.screenSize.rows);
    }
    break;
    case PAGE_DOWN:
    {
        //Threre screenSize.rows > 0 
        editor_.cy = std::min( editor_.rowoff + editor_.screenSize.rows - 1, editor_.numrows() );
        moveCursorPageDown(editor_.screenSize.rows);
    }
    break;

    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
        moveCursor(c);
        break;

    case CTRL_KEY('l'):
    case ESCAPE_SYMBOL:
        break;

    default:
        if (c < 128)
        {
            insertChar(static_cast<char>( c ) );
        }
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
    
    statusMessage.setMessage("HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find");
    
    screenSize = ScreenSize{};
}

EditorConfig::~EditorConfig()
{

}

TerminalEditor::TerminalEditor(int argc, char* argv[])
    : editor_{} // initialize editor_
{
    //1. enableRaw mode
    screenHandle_.enableRawMode();

    //2. screen size initialize inside TerminalEditor.
    editor_.screenSize = screenHandle_.getWindowSize();

    //this already checked.
    /*if (editor_.screenSize.cols <= 0 || editor_.screenSize.rows <= 0)
    {
        die("getWindowSize");
    }*/

    if (editor_.screenSize.rows <= 2)
    {
        die("ScreenSize rows very small!");
    }

    editor_.screenSize.rows -= 2;

    //3. load file if exists.
    if (argc > 1) 
    {
        openFile(argv[1]);
    }

    
}



TerminalEditor::~TerminalEditor()
{
    resetTerminalState();
}


int TerminalEditor::run()
{
    using State = wkilocpp::EditorKeyProcessState;

    while (true)
    {
        refreshScreen();

        State state = processKeypress();

        switch (state) 
        {
        case State::do_continue:
            break;
        case State::do_exit:
            return 0;

        //for future case other states...
        }
    }
}

} // wkilocpp namespace

int main(int argc, char* argv[]) 
{
    try 
    {
        wkilocpp::TerminalEditor terminalEditor(argc, argv);
        
        return terminalEditor.run();
    }
    catch (const std::exception& exception) 
    {
        std::cerr << exception.what() << std::endl;
    }
    catch (...) {
        std::cerr << "Unexpected unknown exception." << std::system_category().message( static_cast<int>( wkilocpp::winGetLastError()) ) << std::endl;;
    }

    return 0;
}