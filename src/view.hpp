#ifndef PPSTEP_VIEW_HPP
#define PPSTEP_VIEW_HPP

#include <vector>
#include <string>
#include <variant>
#include <cstdlib>

#include <sstream>
#include <fstream>
#include <iomanip>
#include <map>
#include <cstring>
#include <functional>
#include <unistd.h>

#include <boost/wave/grammars/cpp_grammar_gen.hpp>

#include <boost/spirit/include/qi.hpp>

#include <boost/filesystem/path.hpp>

#include <linenoise/linenoise.h>

#include "client_fwd.hpp"
#include "server_fwd.hpp"
#include "utils.hpp"


namespace ppstep {

inline std::function<std::vector<std::string>()> g_macro_completer;

inline void linenoise_init() {
    linenoiseHistorySetMaxLen(1000);

    char const* home = getenv("HOME");
    if (home) {
        auto history_path = std::string(home) + "/.ppstep_history";
        linenoiseHistoryLoad(history_path.c_str());
    }

    std::atexit([]{
        char const* home = getenv("HOME");
        if (home) {
            auto history_path = std::string(home) + "/.ppstep_history";
            linenoiseHistorySave(history_path.c_str());
        }
    });

    linenoiseSetCompletionCallback([](char const* buf, linenoiseCompletions* lc) {
        std::string line(buf);
        std::vector<std::string> tokens;
        std::istringstream iss(line);
        std::string tok;
        while (iss >> tok) tokens.push_back(tok);

        bool trailing_space = !line.empty() && (line.back() == ' ');
        std::string prefix = (!trailing_space && !tokens.empty()) ? tokens.back() : "";

        auto add_matching = [&](std::vector<char const*> const& candidates) {
            for (auto cand : candidates) {
                if (strncmp(cand, prefix.c_str(), prefix.size()) == 0)
                    linenoiseAddCompletion(lc, cand);
            }
        };

        // Determine context
        std::string context;
        int n_real_tokens = trailing_space ? (int)tokens.size() : (int)tokens.size() - 1;
        if (n_real_tokens >= 1) context = tokens[0];

        if (context.empty() || tokens.empty() || (!trailing_space && tokens.size() == 1)) {
            // First word: command names
            add_matching({"step", "continue", "finish", "backtrace", "forwardtrace", "break", "delete",
                          "expand", "info", "what", "macros", "list", "set", "help", "quit", "exit", "#define", "#undef", "#include"});
        } else if (context == "b" || context == "break" ||
                   context == "d" || context == "delete") {
            if (n_real_tokens == 1) {
                add_matching({"call", "expand", "rescan", "lex"});
            } else if (n_real_tokens >= 2) {
                if (g_macro_completer) {
                    auto names = g_macro_completer();
                    for (auto& name : names)
                        if (strncmp(name.c_str(), prefix.c_str(), prefix.size()) == 0)
                            linenoiseAddCompletion(lc, name.c_str());
                }
            }
        } else if (context == "e" || context == "expand") {
            if (g_macro_completer) {
                auto names = g_macro_completer();
                for (auto& name : names)
                    if (strncmp(name.c_str(), prefix.c_str(), prefix.size()) == 0)
                        linenoiseAddCompletion(lc, name.c_str());
            }
        } else if (context == "i" || context == "info") {
            add_matching({"breakpoints", "macros", "args", "events"});
        } else if (context == "set" || context == "s") {
            // Only treat `set` as first word — drop the noisy `s` from completion
            // since `s` is already `step`. Just complete subcommands.
            if (context == "set" && n_real_tokens == 1) {
                add_matching({"color", "verbose"});
            }
        }
    });
}

} // namespace ppstep

namespace ppstep::detail {
    template <typename ContextT>
    static bool parse_pp_declaration(ContextT &ctx, std::string const& decl) {
        using position_type = typename ContextT::position_type;
        using iterator_type = typename ContextT::iterator_type;
        
        auto begin = iterator_type(ctx, decl.begin(), decl.end(), position_type("<command line>"));
        auto end = iterator_type();
        
        while (begin != end) {
            ++begin;
        }
        
        return true;
    }
}

namespace ppstep {
    using namespace boost::spirit;

    template <class TokenT, class ContainerT>
    struct client_cli {

        client_cli(client<TokenT, ContainerT>& cl, std::string prefix) : cl(cl), steps_requested(0), prefix(std::move(prefix)) {}

        template <class Attr>
        void step(Attr const& attr) {
            if (attr) {
                auto n = boost::fusion::at_c<1>(*attr);
                steps_requested = (n > 0) ? n : 1;
            } else {
                steps_requested = 1;
            }
            last_repeatable = true;
        }

        template <class Attr>
        void add_breakpoint(Attr const& attr, preprocessing_event_type cond) {
            cl.add_breakpoint({attr.begin(), attr.end()}, cond);
        }

        template <class Attr>
        void remove_breakpoint(Attr const& attr, preprocessing_event_type cond) {
            cl.remove_breakpoint({attr.begin(), attr.end()}, cond);
        }

        void step_continue() {
            steps_requested = 1;
            cl.set_mode(stepping_mode::UNTIL_BREAK);
            last_repeatable = true;
        }
        
        template <class ContextT, class Attr>
        void expand_macro(ContextT& ctx, Attr const& attr) {
            using position_type = typename ContextT::position_type;
            using token_sequence_type = typename ContextT::token_sequence_type;
            using lex_iterator_type = typename ContextT::lexer_type;

            auto macro = std::string(attr.begin(), attr.end());

            auto begin = lex_iterator_type(macro.begin(), macro.end(), position_type("<command line>"), ctx.get_language());
            auto end = lex_iterator_type();

            token_sequence_type pending;
            token_sequence_type expanded;
            bool seen_newline;

            auto old_hooks = std::move(ctx.get_hooks());

            auto new_state = server_state<ContainerT>();
            auto new_client = client<TokenT, ContainerT>(new_state, macro);
            ctx.get_hooks() = server<TokenT, ContainerT>(new_state, new_client);
            auto token = ctx.expand_tokensequence(begin, end, pending, expanded, seen_newline);

            ctx.get_hooks() = std::move(old_hooks);
        }
        
        template <class Context, class Attr>
        void define_macro(Context& ctx, Attr const& attr) {
            auto decl = std::string("#define ");
            decl.insert(decl.end(), attr.begin(), attr.end());
            detail::parse_pp_declaration(ctx, decl);
        }
        
        template <class Context, class Attr>
        void undefine_macro(Context& ctx, Attr const& attr) {
            auto decl = std::string("#undef ");
            decl.insert(decl.end(), attr.begin(), attr.end());
            detail::parse_pp_declaration(ctx, decl);
        }
        
        template <class Context, class Attr>
        void include_file(Context& ctx, Attr const& attr) {
            auto decl = std::string("#include ");
            decl.insert(decl.end(), attr.begin(), attr.end());
            detail::parse_pp_declaration(ctx, decl);
        }
        
        template <class Context>
        void show_macros(Context const& ctx) {
            for (auto it = ctx.macro_names_begin(); it != ctx.macro_names_end(); ++it) {
                if (it->rfind("__", 0) == 0) continue; // predefined macro

                bool has_params, is_predefined;
                typename Context::position_type pos;
                std::vector<typename Context::token_type> parameters;
                typename Context::token_sequence_type definition;
                ctx.get_macro_definition(*it, has_params, is_predefined, pos, parameters, definition);
                
                std::cout << " - " << *it;
                if (has_params) {
                    std::cout << '(';

                    auto params_it = parameters.begin();
                    auto params_end = parameters.end();

                    if (params_it != params_end)
                        std::cout << (params_it++)->get_value();

                    for (; params_it != params_end; ++params_it)
                        std::cout << ", " << params_it->get_value();

                    std::cout << ')';

                }
                
                std::cout << " ";
                for (auto const& token : definition) {
                    std::cout << token.get_value();
                }
                std::cout << '\n';
            }
            std::cout << std::flush;
        }
        
        void expanding_trace(int limit = -1) {
            auto const& expanding = cl.get_state().expanding;

            std::size_t idx = 0;
            auto it = expanding.rbegin();
            auto end = expanding.rend();
            for (; it != end && (limit < 0 || idx < (std::size_t)limit); ++it, ++idx) {
                std::cout << idx << ": ";
                print_token_container(std::cout, *it) << std::endl;
            }
            if (it != end)
                std::cout << "... (" << (expanding.size() - idx) << " more frames)\n" << std::flush;
            else
                std::cout << std::flush;
        }

        void rescanning_trace(int limit = -1) {
            auto const& rescanning = cl.get_state().rescanning;

            std::size_t idx = 0;
            auto it = rescanning.rbegin();
            auto end = rescanning.rend();
            for (; it != end && (limit < 0 || idx < (std::size_t)limit); ++it, ++idx) {
                auto const& [cause, initial] = *it;
                std::cout << idx << ": ";
                print_token_container(std::cout, initial) << '\n';

                std::size_t padding_width = idx == 0 ? 1 : 0;
                for (std::size_t i = idx; i != 0; i /= 10) {
                    ++padding_width;
                }
                std::cout << std::string(padding_width, ' ') << "  caused by ";
                print_token_container(std::cout, cause) << std::endl;
            }
            if (it != end)
                std::cout << "... (" << (rescanning.size() - idx) << " more frames)\n" << std::flush;
            else
                std::cout << std::flush;
        }

        void show_help() {
            std::cout <<
                "ppstep — C preprocessor macro-expansion debugger\n"
                "\n"
                "Stepping:\n"
                "  step [N] / s [N]       Step forward N preprocessing events (default 1)\n"
                "  finish / fin            Run until current macro expansion completes\n"
                "  continue / c            Continue until breakpoint or end\n"
                "\n"
                "Breakpoints:\n"
                "  break <type> <macro> / b <t> <m>   Set breakpoint\n"
                "  delete <type> <macro> / d <t> <m>  Remove breakpoint by name\n"
                "  delete <N> / d <N>                 Remove breakpoint by number\n"
                "  delete / d                         Remove all breakpoints\n"
                "  info breakpoints / i b             List breakpoints\n"
                "    types: call/c, expand/e, rescan/r, lex/l\n"
                "  info args / i a                    Show arg → param bindings at current call\n"
                "\n"
                "Inspection:\n"
                "  list / l                 Show source code around current position\n"
                "  backtrace [N] / bt [N]   Show expansion stack (last N frames)\n"
                "  forwardtrace [N] / ft [N] Show rescanning stack (last N frames)\n"
                "  info macros / i m        List defined macros\n"
                "  info events / i e        Show recent preprocessing events\n"
                "  what / ?                 Explain current event\n"
                "\n"
                "Manipulation:\n"
                "  expand <macro> / e <m>   Expand a macro and show result\n"
                "  #define <name> <body>     Define a macro mid-session\n"
                "  #undef <name>            Undefine a macro mid-session\n"
                "  #include <file>          Include a file mid-session\n"
                "\n"
                "Display:\n"
                "  set color always|auto|never / s c al|au|ne\n"
                "                            Color fg highlighting by token category\n"
                "                              (ident / lit / str / kw / op)\n"
                "  set verbose on|off / s v on|off\n"
                "                            Verbose per-event panels (placeholder)\n"
                "\n"
                "Other:\n"
                "  help                     Show this help\n"
                "  quit / q                 Exit debugger\n"
                "\n"
                "Empty Enter repeats the last stepping command (step/continue).\n"
                "Commands accept unambiguous prefixes (e.g., 'st' = step, 'con' = continue).\n"
                << std::flush;
        }

        void quit() {
            throw session_terminate();
        }

        void set_color_always() {
            ppstep::set_color_enabled(true);
            std::cout << "Color: always (fg highlighting by token category)\n" << std::flush;
        }
        void set_color_never() {
            ppstep::set_color_enabled(false);
            std::cout << "Color: never\n" << std::flush;
        }
        void set_color_auto() {
            // TTY-detect at evaluation time
            ppstep::set_color_enabled(isatty(STDOUT_FILENO) != 0);
            std::cout << "Color: auto (now "
                      << (ppstep::color_enabled() ? "on" : "off")
                      << ")\n" << std::flush;
        }
        void set_verbose(bool on) {
            cl.set_verbose(on);
            std::cout << "Verbose mode: " << (on ? "on" : "off")
                      << " — per-event panels (source context + macro definition)\n" << std::flush;
        }

        void finish() {
            auto const& st = cl.get_state();
            if (st.expanding.empty()) {
                std::cout << "Not currently expanding a macro. "
                             "Step until the call site is reached, then `finish`.\n" << std::flush;
                return;
            }
            auto const& top = st.expanding.back();
            std::cout << "Finishing expansion of: "
                      << (top.empty() ? "<empty>" : std::string(top.begin()->get_value().c_str()))
                      << "  (depth " << st.expanding.size() << ")\n" << std::flush;
            cl.arm_finish();
            cl.set_mode(stepping_mode::UNTIL_BREAK);
            last_repeatable = true;
        }

        void list_breakpoints() {
            auto const& bps = cl.list_breakpoints();
            if (bps.empty()) {
                std::cout << "No breakpoints.\n" << std::flush;
                return;
            }
            std::cout << "Num  Type     What\n";
            for (auto const& bp : bps) {
                std::cout << '#' << bp.id << "   " << cl.bp_type_name(bp.type) << "      " << bp.name << '\n';
            }
            std::cout << std::flush;
        }

        void remove_breakpoint_by_id(unsigned int id) {
            if (!cl.remove_breakpoint(static_cast<int>(id))) {
                std::cout << "No breakpoint number " << id << ".\n" << std::flush;
            }
        }

        template <class Context>
        void show_recent_events(Context const& ctx, unsigned int n = 10) {
            auto it = cl.newest_history();
            auto end = cl.oldest_history();
            for (unsigned int i = 0; i < n && it != end; ++i, ++it) {
                std::cout << i << ": ";
                std::visit([&it](auto const& event){ event.print(std::cout, it->tokens); }, it->event);
            }
            std::cout << std::flush;
        }

        template <class ContextT>
        void show_info_summary(ContextT& ctx) {
            auto const& st = cl.get_state();

            // ─── expanding context ───
            std::cout << "═══ expanding context (depth " << st.expanding.size() << ") ═══\n";
            if (st.expanding.empty()) {
                std::cout << "  Not currently inside any macro expansion.\n";
            } else {
                auto print_frame = [&](char const* label, ContainerT const& frame) {
                    if (frame.empty()) {
                        std::cout << "  " << label << " : (empty frame)\n\n";
                        return;
                    }
                    std::string name(frame.begin()->get_value().c_str());
                    std::cout << "  " << label << " : " << name;

                    bool has_params = false, is_predefined = false;
                    typename ContextT::position_type pos;
                    std::vector<typename ContextT::token_type> parameters;
                    typename ContextT::token_sequence_type definition;
                    try {
                        ctx.get_macro_definition(name, has_params, is_predefined, pos, parameters, definition);
                    } catch (...) {
                        std::cout << "  (lookup failed)\n\n";
                        return;
                    }

                    std::cout << "\n    #define " << name;
                    if (has_params) {
                        std::cout << '(';
                        for (std::size_t i = 0; i < parameters.size(); ++i) {
                            if (i) std::cout << ", ";
                            std::cout << parameters[i].get_value().c_str();
                        }
                        std::cout << ')';
                    }
                    if (is_predefined) std::cout << "  (predefined)";
                    std::cout << "\n    body   : ";
                    for (auto const& t : definition) std::cout << t.get_value().c_str();
                    std::cout << "\n\n";
                };

                print_frame("root     ", st.expanding.front());
                if (st.expanding.size() > 1) {
                    print_frame("expanding", st.expanding.back());
                } else {
                    std::cout << "  (single-frame — root and expanding are the same)\n\n";
                }
            }

            // ─── pending rescans ───
            std::cout << "── pending rescans (" << st.rescanning.size() << ") ──\n";
            if (st.rescanning.empty()) {
                std::cout << "  (none)\n";
            } else {
                std::size_t idx = 0;
                for (auto const& frame : st.rescanning) {
                    auto const& cause = frame.first;
                    auto const& initial = frame.second;
                    std::cout << "  " << idx++ << ": ";
                    if (!cause.empty()) {
                        std::cout << "cause=" << cause.begin()->get_value().c_str();
                    } else {
                        std::cout << "cause=?";
                    }
                    std::cout << "    over: ";
                    print_token_container(std::cout, initial);
                    std::cout << "\n";
                }
            }

            // ─── recent events (last 5) ───
            std::cout << "── recent events ──\n";
            {
                constexpr int N = 5;
                int shown = 0;
                auto it = cl.newest_history();
                auto end = cl.oldest_history();
                for (int i = 0; i < N && it != end; ++i, ++it) {
                    ++shown;
                    std::visit([&](auto const& e) {
                        e.print_summary(std::cout);
                    }, it->event);
                }
                if (shown == 0) std::cout << "  (none yet)\n";
            }

            // ─── counts (across token_history) ───
            {
                std::map<std::string, int> kind_counts;
                std::map<std::string, int> macro_counts;
                // Iterate newest → oldest (forward in reverse-iterator land;
                // ++ on rend() would be UB).
                for (auto h = cl.newest_history(); h != cl.oldest_history(); ++h) {
                    std::visit([&](auto const& e) {
                        kind_counts[e.event_kind()]++;
                        if (auto n = e.event_macro_name()) {
                            macro_counts[*n]++;
                        }
                    }, h->event);
                }
                std::cout << "── counts ──\n";
                if (kind_counts.empty()) {
                    std::cout << "  (no events yet)\n";
                } else {
                    std::cout << "  ";
                    for (auto const& [k, v] : kind_counts) {
                        std::cout << k << ":" << v << "  ";
                    }
                    std::cout << "\n";
                    if (!macro_counts.empty()) {
                        std::cout << "  per macro: ";
                        bool first = true;
                        for (auto const& [k, v] : macro_counts) {
                            if (!first) std::cout << ", ";
                            first = false;
                            std::cout << k << " ×" << v;
                        }
                        std::cout << "\n";
                    }
                }
            }

            // ─── mode status ───
            std::cout << "── mode ──\n";
            std::cout << "  finish-pending: " << (cl.is_finish_armed() ? "yes" : "no") << "\n";
            std::cout << "  verbose:        " << (cl.is_verbose() ? "on" : "off") << "\n";
            std::cout << "  last cmd:       "
                      << (last_command.empty() ? "(none yet)" : last_command)
                      << "\n";
            std::cout << "  breakpoints:    " << cl.list_breakpoints().size() << " active\n";

            std::cout << std::flush;
        }

        template <class ContextT>
        void show_args(ContextT& ctx) {
            auto latest = cl.newest_history();
            if (latest == cl.oldest_history()) {
                std::cout << "No current event.\n" << std::flush;
                return;
            }
            std::visit([&ctx](auto const& event) {
                event.print_args(std::cout, ctx);
            }, latest->event);
        }
        
        void explain_current_state() {
            auto latest = cl.newest_history();
            if (latest == cl.oldest_history())
                return;
            
            std::visit([&latest](auto const& event){ event.explain(std::cout); }, latest->event);
        }

        template <class ContextT>
        void current_state(ContextT& ctx) {
            auto latest = cl.newest_history();
            if (latest == cl.oldest_history())
                return;

            auto pos = ctx.get_main_pos();
            auto pos_file = boost::filesystem::path(pos.get_file().begin(), pos.get_file().end()).filename().string();
            std::cout << '[' << pos_file << ':' << pos.get_line() << ':'  << pos.get_column() << "]: ";

            if (cl.is_verbose()) {
                std::cout << '\n';
                print_source_context(std::cout, ctx, 2);
            }

            std::visit([this, &ctx, &latest](auto const& event){
                if (cl.is_verbose()) event.print_verbose(std::cout, latest->tokens, ctx);
                else                  event.print(std::cout, latest->tokens);
            }, latest->event);
        }

        template <class ContextT>
        static void print_source_context(std::ostream& os, ContextT& ctx, int context_lines) {
            auto pos = ctx.get_main_pos();
            auto file = std::string(pos.get_file().begin(), pos.get_file().end());
            if (file.empty() || file.rfind("<", 0) == 0) {
                os << "  (no source file for current position)\n";
                return;
            }

            static std::map<std::string, std::vector<std::string>> file_cache;
            auto& lines = file_cache[file];
            if (lines.empty()) {
                std::ifstream f(file);
                if (!f) {
                    os << "  (cannot read source file: " << file << ")\n";
                    return;
                }
                std::string line;
                if (!std::getline(f, lines.emplace_back())) {
                    os << "  (empty source file: " << file << ")\n";
                    lines.clear();
                    file_cache.erase(file);
                    return;
                }
                while (std::getline(f, line)) lines.push_back(line);
            }

            int current = static_cast<int>(pos.get_line());
            int start = std::max(0, current - context_lines - 1);
            int end = std::min(static_cast<int>(lines.size()), current + context_lines);

            auto filename = boost::filesystem::path(file).filename().string();
            int num_width = std::to_string(end).size();

            for (int i = start; i < end; ++i) {
                char marker = (i == current - 1) ? '>' : ' ';
                os << "  " << marker << " "
                   << std::setw(num_width) << (i + 1) << " | "
                   << lines[i] << '\n';
            }
        }

        template <class ContextT>
        void list_source(ContextT& ctx, int context_lines = 5) {
            print_source_context(std::cout, ctx, context_lines);
            std::cout << std::flush;
        }

        template <class ContextT, typename Iterator>
        bool parse(ContextT& ctx, Iterator first, Iterator last) {
            using qi::lit;
            using qi::print;
            using qi::uint_;
            using qi::alpha;
            using qi::lexeme;
            using qi::alnum;
            using qi::eoi;
            using qi::eol;
            using qi::phrase_parse;
            using ascii::char_;
                        using ascii::space;
                        using ascii::space_type;
            
                        auto anything = +(print);
            
            #define PPSTEP_ACTION(...) ([this, &ctx](auto const& attr){ __VA_ARGS__; })
            
                        qi::rule<Iterator, ascii::space_type> grammar =
                            // Quit first: `q` is one character and would otherwise
                            // commit inside the step/continue/expand rules (each
                            // has a single-letter alias) before this rule is reached.
                            (lit("quit") | lit("q") | lit("exit"))[PPSTEP_ACTION(quit())]
                          | lexeme[lit("set") >> +space >> (
                              ( lit("color") >> +space >> (
                                  lit("always")[PPSTEP_ACTION(set_color_always())]
                                | lit("on")[PPSTEP_ACTION(set_color_always())]
                                | lit("never")[PPSTEP_ACTION(set_color_never())]
                                | lit("off")[PPSTEP_ACTION(set_color_never())]
                                | lit("auto")[PPSTEP_ACTION(set_color_auto())]
                              ))
                            | ( lit("verbose") >> +space >> (
                                  lit("on")[PPSTEP_ACTION(set_verbose(true))]
                                | lit("true")[PPSTEP_ACTION(set_verbose(true))]
                                | lit("off")[PPSTEP_ACTION(set_verbose(false))]
                                | lit("false")[PPSTEP_ACTION(set_verbose(false))]
                              ))
                            )]
                          | lexeme[(lit("step") | lit("s")) >> -(+space >> uint_)][PPSTEP_ACTION(step(attr))]
                          | (lit("finish") | lit("fin"))[PPSTEP_ACTION(finish())]
                          | (lit("continue") | lit("c"))[PPSTEP_ACTION(step_continue())]
                          | lexeme[((lit("backtrace") | lit("bt")) >> +space >> uint_)[PPSTEP_ACTION(expanding_trace(boost::fusion::at_c<1>(attr)))]]
                          | lexeme[(lit("backtrace") | lit("bt"))[PPSTEP_ACTION(expanding_trace())]]
                          | lexeme[((lit("forwardtrace") | lit("ft")) >> +space >> uint_)[PPSTEP_ACTION(rescanning_trace(boost::fusion::at_c<1>(attr)))]]
                          | lexeme[(lit("forwardtrace") | lit("ft"))[PPSTEP_ACTION(rescanning_trace())]]
                          | lexeme[
                              (lit("break") | lit("b")) >> *space > (
                                    ((lit("call") | lit("c")) > +space > anything[PPSTEP_ACTION(add_breakpoint(attr, preprocessing_event_type::CALL))])
                                  | ((lit("expand") | lit("e")) > +space > anything[PPSTEP_ACTION(add_breakpoint(attr, preprocessing_event_type::EXPANDED))])
                                  | ((lit("rescan") | lit("r")) > +space > anything[PPSTEP_ACTION(add_breakpoint(attr, preprocessing_event_type::RESCANNED))])
                                  | ((lit("lex") | lit("l")) > +space > anything[PPSTEP_ACTION(add_breakpoint(attr, preprocessing_event_type::LEXED))])
                            )]
                          | lexeme[
                              (lit("delete") | lit("d")) >> *space > (
                                    ((lit("call") | lit("c")) > +space > anything[PPSTEP_ACTION(remove_breakpoint(attr, preprocessing_event_type::CALL))])
                                  | ((lit("expand") | lit("e")) > +space > anything[PPSTEP_ACTION(remove_breakpoint(attr, preprocessing_event_type::EXPANDED))])
                                  | ((lit("rescan") | lit("r")) > +space > anything[PPSTEP_ACTION(remove_breakpoint(attr, preprocessing_event_type::RESCANNED))])
                                  | ((lit("lex") | lit("l")) > +space > anything[PPSTEP_ACTION(remove_breakpoint(attr, preprocessing_event_type::LEXED))])
                            )]
                          | lexeme[(lit("expand") | lit("e")) > +space > anything[PPSTEP_ACTION(expand_macro(ctx, attr))]]
                          | lexeme[lit("#define") > +space > anything[PPSTEP_ACTION(define_macro(ctx, attr))]]
                          | lexeme[lit("#undef") > +space > anything[PPSTEP_ACTION(undefine_macro(ctx, attr))]]
                          | lexeme[lit("#include") > +space > anything[PPSTEP_ACTION(include_file(ctx, attr))]]

                          | ((lit("info") | lit("i")) >> eoi)[PPSTEP_ACTION(show_info_summary(ctx))]
                          | lexeme[(lit("info") | lit("i")) >> +space > (
                                (lit("breakpoints") | lit("b"))[PPSTEP_ACTION(list_breakpoints())]
                              | (lit("macros") | lit("m"))[PPSTEP_ACTION(show_macros(ctx))]
                              | (lit("args") | lit("a"))[PPSTEP_ACTION(show_args(ctx))]
                              | (lit("events") | lit("e"))[PPSTEP_ACTION(show_recent_events(ctx))]
                              | (lit("events") | lit("e")) >> +space >> uint_[PPSTEP_ACTION(show_recent_events(ctx, attr))]
                            )]

                          | lexeme[(lit("delete") | lit("d")) >> +space >> uint_[PPSTEP_ACTION(remove_breakpoint_by_id(attr))]]

                          | (lit("what") | lit("?"))[PPSTEP_ACTION(explain_current_state())]
                          | (lit("list") | lit("l"))[PPSTEP_ACTION(list_source(ctx))]
                          | lit("macros")[PPSTEP_ACTION(show_macros(ctx))]
                          | (lit("help") | lit("h") | lit("i"))[PPSTEP_ACTION(show_help())]
                          | eoi[PPSTEP_ACTION(current_state(ctx))];
            
            #undef PPSTEP_ACTION
            
                        qi::on_error<qi::fail>(grammar, [](auto const& args, auto const& ctx, auto const&) {
                            std::cout << "Found unexpected argument \"" << boost::fusion::at_c<2>(args) << "\" while parsing \"" << boost::fusion::at_c<0>(args) << "\". Expected: " << boost::fusion::at_c<3>(args) << std::endl;
                        });
            
                        bool r = phrase_parse(first, last, grammar, space);
                        if (first != last) {
                            return false;
            }
            return r;
        }

        template <class ContextT>
        void prompt(ContextT& ctx, std::string const& trigger, bool print_state = true) {
            if (steps_requested > 0) --steps_requested;
            if (steps_requested) return;

            cl.set_mode(stepping_mode::FREE);

            // Set up macro name completion for this prompt session
            g_macro_completer = [&ctx]() -> std::vector<std::string> {
                std::vector<std::string> names;
                for (auto it = ctx.macro_names_begin(); it != ctx.macro_names_end(); ++it) {
                    std::string name(it->c_str());
                    if (name.rfind("__", 0) != 0) // skip predefined
                        names.push_back(std::move(name));
                }
                return names;
            };

            if (print_state) current_state(ctx);

            // Print a banner ABOVE the prompt line — used to surface the
            // `#define` of the macro being entered, so the user can see
            // what the call is about to substitute without leaving the prompt.
            if (!trigger.empty() && trigger.compare(0, 8, "calling ") == 0) {
                std::string name = trigger.substr(8);  // strip "calling " prefix
                // Macro names don't contain spaces; trim any trailing junk
                // (e.g. if the trigger had extra characters appended).
                auto sp = name.find(' ');
                if (sp != std::string::npos) name = name.substr(0, sp);
                std::cout << cl.make_calling_banner(name, ctx);
            } else if (!trigger.empty() && trigger.compare(0, 10, "rescanned ") == 0) {
                std::string name = trigger.substr(10);
                auto sp = name.find(' ');
                if (sp != std::string::npos) name = name.substr(0, sp);
                std::cout << cl.make_rescanned_banner(name);
            }

            auto prompt = std::string("pp");
            if (!prefix.empty()) {
                prompt += " [" + prefix + ']';
            }
            if (!trigger.empty()) {
                prompt += " (" + trigger + ')';
            }
            prompt += "> ";

            for (char* raw_line; (raw_line = linenoise(prompt.c_str())) != nullptr;) {
                bool empty = (raw_line[0] == '\0');
                if (empty && last_command.empty()) {
                    // No stepping command has been issued yet — empty
                    // <Enter> is a no-op rather than running undefined state.
                    std::free(static_cast<void*>(raw_line));
                    continue;
                }

                char const* cmd = empty ? last_command.c_str() : raw_line;
                if (!empty) {
                    linenoiseHistoryAdd(raw_line);
                }

                last_repeatable = false;

                // Prefix abbreviation resolution
                std::string resolved_cmd;
                if (!empty) {
                    std::string line(cmd);
                    auto space_pos = line.find(' ');
                    auto first_word = line.substr(0, space_pos);
                    auto rest = (space_pos != std::string::npos) ? line.substr(space_pos) : std::string();

                    // Known first-word tokens the grammar accepts
                    static std::vector<std::pair<char const*, char const*>> commands = {
                        {"step","step"}, {"s","step"},
                        {"continue","continue"}, {"c","continue"},
                        {"backtrace","backtrace"}, {"bt","backtrace"},
                        {"forwardtrace","forwardtrace"}, {"ft","forwardtrace"},
                        {"break","break"}, {"b","break"},
                        {"delete","delete"}, {"d","delete"},
                        {"expand","expand"}, {"e","expand"},
                        {"info","info"}, {"i","info"},
                        {"what","what"}, {"?","what"},
                        {"macros","macros"},
                        {"list","list"}, {"l","list"},
                        {"set","set"},
                        {"quit","quit"}, {"q","quit"}, {"exit","quit"},
                        {"#define","#define"}, {"#undef","#undef"}, {"#include","#include"},
                    };

                    // Check if first_word is an exact match
                    bool exact = false;
                    for (auto& [key, _] : commands) {
                        if (first_word == key) { exact = true; break; }
                    }

                    if (!exact) {
                        std::vector<char const*> matches;
                        for (auto& [key, canon] : commands) {
                            if (strncmp(key, first_word.c_str(), first_word.size()) == 0)
                                matches.push_back(canon);
                        }
                        // Deduplicate canonical names
                        std::sort(matches.begin(), matches.end());
                        matches.erase(std::unique(matches.begin(), matches.end()), matches.end());

                        if (matches.size() == 1) {
                            resolved_cmd = std::string(matches[0]) + rest;
                            cmd = resolved_cmd.c_str();
                        } else if (matches.size() > 1) {
                            std::cout << "Ambiguous command \"" << first_word << "\": ";
                            for (size_t i = 0; i < matches.size(); ++i) {
                                if (i) std::cout << ", ";
                                std::cout << matches[i];
                            }
                            std::cout << ".\n" << std::flush;
                            std::free(static_cast<void*>(raw_line));
                            continue;
                        }
                        // 0 matches: leave as-is, parse will report "Undefined command"
                    }
                }

                bool valid = parse(ctx, cmd, cmd + std::strlen(cmd));
                if (!valid) {
                    std::cout << "Undefined command: \"" << cmd << "\"." << std::endl;
                } else if (last_repeatable) {
                    // Only stepping commands (step / continue / finish) update
                    // the <Enter>-repeat slot. Non-stepping commands
                    // (help, list, break, delete, info, define, etc.) leave
                    // last_command alone, so empty <Enter> keeps replaying
                    // the most recent stepping command.
                    last_command = cmd;
                }

                std::free(static_cast<void*>(raw_line));

                if (valid) {
                    if (steps_requested) break;
                }
            }
        }

    private:
        client<TokenT, ContainerT>& cl;
        std::size_t steps_requested;
        std::string prefix;
        std::string last_command;
        bool last_repeatable = false;
    };
}

#endif // PPSTEP_VIEW_HPP
