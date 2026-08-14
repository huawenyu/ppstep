#ifndef PPSTEP_UTILS_HPP
#define PPSTEP_UTILS_HPP

#include <algorithm>
#include <utility>
#include <vector>
#include <optional>
#include <iostream>
#include <string>

#include <boost/wave/token_ids.hpp>
#include <boost/wave/cpp_exceptions.hpp>

namespace ppstep {

    // Display width of a UTF-8 string in terminal columns. The frames log
    // uses box-drawing chars (── │ ┌ └) which are multi-BYTE but single-COLUMN;
    // padding by std::string::size() (byte count) over-counts them and pushes
    // the `|` split column left of the ASCII-only rows. This counts UTF-8
    // code points instead, which equals the column count for the 1-col-wide
    // glyphs used here. (Wide/CJK chars would need a real wcwidth — not used.)
    inline std::size_t display_width(std::string const& s) {
        std::size_t n = 0;
        for (std::size_t i = 0; i < s.size(); ) {
            unsigned char c = static_cast<unsigned char>(s[i]);
            // Advance one UTF-8 code point: 1-byte (0xxxxxxx) or count the
            // leading 1-bits for multi-byte (11xxxxxx).
            if (c < 0x80) ++i;
            else if ((c & 0xE0) == 0xC0) i += 2;
            else if ((c & 0xF0) == 0xE0) i += 3;
            else if ((c & 0xF8) == 0xF0) i += 4;
            else ++i;  // invalid — treat as one
            ++n;
        }
        return n;
    }

    // Format a boost::wave cpp_exception as a compiler-style diagnostic:
    //   <file>:<line>:<col>: <severity>: <error_text>[: <detail>]
    // Wave's exception `what()` returns the useless literal type name
    // ("boost::wave::preprocess_exception"). The real payload is
    // `description()`, which Wave builds as "<severity>: <error_text>[: <msg>]"
    // (see boost/wave/cpp_throw.hpp). So we only prepend the location — the
    // severity label is already inside `description()`.
    template <class Exception>
    std::ostream& print_diagnostic(std::ostream& os, Exception const& e) {
        char const* file = e.file_name();
        if (file && *file) os << file << ':';
        os << e.line_no() << ':' << e.column_no() << ": ";
        os << e.description();
        return os;
    }

    // Color toggles, set by REPL `set color` command and `main()` init.
    // Threaded through `print_token`; with color disabled the output is
    // byte-identical to the original (no ANSI escapes).
    inline bool g_color_enabled = false;

    inline bool color_enabled() { return g_color_enabled; }
    inline void set_color_enabled(bool on) { g_color_enabled = on; }

    namespace ansi {
        // Four-color highlight scheme for the working macro span — one
        // color per preprocessing event, so each phase of a macro's
        // lifecycle is visually distinct:
        //   white   #F8F8F2 — call      (the macro is about to expand)
        //   yellow  #      — expanded   (substitution done, body produced)
        //   cyan    #      — rescanned  (rescan pass over the body)
        //   green   #50FA7B — done      (whole expansion tree complete)
        // A background fill (highlighter-marker style) makes the in-flight
        // macro jump out of the token stream; black fg (30m) keeps the
        // macro name readable on the colored background. The frames-log
        // closer mirrors the scheme with the matching fg colors (green for
        // , white for ).
        constexpr auto red_fg     = "[91m";                // working (closer)
        constexpr auto green_fg   = "[38;2;80;250;123m";   // done
        constexpr auto yellow_fg  = "[93m";                // expanded
        constexpr auto cyan_fg    = "[96m";                // rescanned
        constexpr auto white_fg   = "[38;2;248;248;242m"; // call
        constexpr auto green_bg   = "[48;2;80;250;123m";  // done
        constexpr auto yellow_bg  = "[43m";                // expanded
        constexpr auto cyan_bg    = "[46m";                // rescanned
        constexpr auto white_bg   = "[48;2;248;248;242m"; // call
        constexpr auto black_fg   = "[30m";

        constexpr auto underline = "[4m";
        constexpr auto bold      = "[1m";

        constexpr auto reset = "[0m";
    }

    // ANSI fg color codes per token category. ESC = \x1b.
    inline char const* token_fg_color(boost::wave::token_id id) {
        using namespace boost::wave;
        switch (id) {
            case T_IDENTIFIER:
                return "\x1b[37;1m";
            // Literals / numbers
            case T_INTLIT:
            case T_LONGINTLIT:
            case T_OCTALINT:
            case T_DECIMALINT:
            case T_HEXAINT:
            case T_FLOATLIT:
            case T_CHARLIT:
            case T_PP_NUMBER:
                return "\x1b[35m";
            // Strings
            case T_STRINGLIT:
                return "\x1b[32m";
            default:
                break;
        }
        return "";
    }

    template <class Token>
    inline char const* token_fg_color_for(Token const& token) {
        using namespace boost::wave;
        if (IS_CATEGORY(token, KeywordTokenType))
            return "\x1b[36;1m";
        if (IS_CATEGORY(token, OperatorTokenType))
            return "\x1b[33m";
        if (IS_CATEGORY(token, StringLiteralTokenType))
            return "\x1b[32m";
        if (IS_CATEGORY(token, LiteralTokenType))
            return "\x1b[35m";
        return token_fg_color(token_id(token));
    }

    template <class Token>
    std::ostream& print_token(std::ostream& os, Token const& token) {
        if (!g_color_enabled) {
            os << token.get_value().c_str();
            return os;
        }
        char const* fg = token_fg_color_for(token);
        if (*fg) {
            os << fg << token.get_value().c_str();
        } else {
            os << token.get_value().c_str();
        }
        return os;
    }

    template <class Iterator, class Printer, class Delimiter>
    std::ostream& print_with_delimiter(std::ostream& os, Iterator& it, Iterator end, Printer printer, Delimiter const& delimiter) {
        if (it == end) return os;

        printer(os, *it++);
        for (; it != end; ++it) {
            os << delimiter;
            printer(os, *it);
        }
        return os;
    }

    template <class Iterator>
    std::ostream& print_token_range(std::ostream& os, Iterator& it, Iterator end) {
        return print_with_delimiter(os, it, std::move(end), [](auto& os, auto const& token) { print_token(os, token); }, ' ');
    }

    // Print tokens as bare values, ignoring per-token fg colors — the caller
    // has already set a highlight color (the event's bright magenta/yellow/cyan
    // + bold) and the per-token fg would override it, erasing the highlight.
    // Used for the highlighted macro-name span in event output.
    template <class Iterator>
    std::ostream& print_token_range_plain(std::ostream& os, Iterator& it, Iterator end) {
        return print_with_delimiter(os, it, std::move(end), [](auto& os, auto const& token) {
            os << token.get_value().c_str();
        }, ' ');
    }

    template <class Container>
    std::ostream& print_token_container(std::ostream& os, Container const& data) {
        auto it = std::begin(data);
        return print_token_range(os, it, std::end(data));
    }

    template <class Container, class T>
    auto join_lists(Container const& lists, T const& separator) {
        auto acc = std::vector<T>();

        auto it = std::begin(lists);
        {
            auto const& list = *it++;
            acc.insert(std::end(acc), std::begin(list), std::end(list));
        }

        auto end = std::end(lists);
        for (; it != end; ++it) {
            acc.push_back(separator);
            acc.insert(std::end(acc), std::begin(*it), std::end(*it));
        }

        return acc;
    }

    template <class Container>
    std::optional<std::pair<typename Container::const_iterator, typename Container::const_iterator>>
    find_sublist(Container const& data, Container const& pattern, typename Container::const_iterator it) {
        auto end = std::end(data);
        auto match = std::search(it, end, std::begin(pattern), std::end(pattern));
        if (match != end) {
            return {{match, std::next(match, pattern.size())}};
        } else {
            return {};
        }
    }
}

#endif // PPSTEP_UTILS_HPP
