#ifndef PPSTEP_UTILS_HPP
#define PPSTEP_UTILS_HPP

#include <algorithm>
#include <utility>
#include <vector>
#include <optional>
#include <iostream>

#include <boost/wave/token_ids.hpp>

namespace ppstep {

    // Color toggles, set by REPL `set color` command and `main()` init.
    // Threaded through `print_token`; with color disabled the output is
    // byte-identical to the original (no ANSI escapes).
    inline bool g_color_enabled = false;

    inline bool color_enabled() { return g_color_enabled; }
    inline void set_color_enabled(bool on) { g_color_enabled = on; }

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
