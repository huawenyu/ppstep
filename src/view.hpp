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
            add_matching({"breakpoints", "macros", "args", "events", "stack", "disabled", "macro"});
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
                // `step N`: step over exactly N preprocessing events of any
                // kind (including LEXED tokens) — fine-grained token-level
                // stepping when the user asks for a specific count.
                auto n = boost::fusion::at_c<1>(*attr);
                steps_requested = (n > 0) ? n : 1;
                cl.set_mode(stepping_mode::FREE);
            } else {
                // Bare `step`: behave like `next` — skip LEXED (non-macro)
                // events and stop at the next macro event (call/expand/rescan).
                // Stopping at every single source token by default is noisy and
                // rarely useful; the user can `step 1` for that.
                steps_requested = 1;
                cl.set_mode(stepping_mode::UNTIL_MACRO);
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

        void step_next() {
            steps_requested = 1;
            cl.set_mode(stepping_mode::UNTIL_MACRO);
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
        
        // `bt` / `backtrace`: show the expansion call stack. With `full=true`
        // (the `bt full` form) each frame also prints its bound param←arg
        // bindings — the gdb `bt full` analogue. Frame #0 is the innermost
        // (currently-expanding) call.
        template <class ContextT>
        void expanding_trace(ContextT& ctx, int limit = -1, bool full = false) {
            auto const& expanding = cl.get_state().expanding;

            std::size_t idx = 0;
            auto it = expanding.rbegin();
            auto end = expanding.rend();
            for (; it != end && (limit < 0 || idx < (std::size_t)limit); ++it, ++idx) {
                std::cout << "#" << idx << "  ";
                if (it->call.empty()) {
                    std::cout << "(empty frame)\n";
                    continue;
                }
                std::string name(it->call.begin()->get_value().c_str());
                std::cout << name;
                if (idx == 0) std::cout << "  <- current";
                std::cout << '\n';

                if (full) {
                    print_frame_args(std::cout, ctx, name, it->arguments);
                }
            }
            if (it != end)
                std::cout << "... (" << (expanding.size() - idx) << " more frames)\n";
            std::cout << std::flush;
        }

        // Pretty-print the param←arg bindings for one expansion frame. Looks
        // up the macro's formal parameters via the wave context so the labels
        // match the definition, then pairs them with the argument token-lists
        // captured on the frame. Shared by `bt full` and `info stack`.
        template <class ContextT>
        static void print_frame_args(std::ostream& os, ContextT& ctx,
                                     std::string const& name,
                                     std::vector<ContainerT> const& arguments) {
            bool has_params = false, is_predefined = false;
            typename ContextT::position_type pos;
            std::vector<typename ContextT::token_type> parameters;
            typename ContextT::token_sequence_type definition;
            try {
                ctx.get_macro_definition(name, has_params, is_predefined, pos, parameters, definition);
            } catch (...) {
                os << "    (definition lookup failed)\n";
                return;
            }
            if (!has_params) {
                os << "    object-like — no arguments\n";
                return;
            }
            if (parameters.empty() && arguments.empty()) {
                os << "    (no parameters, no arguments)\n";
                return;
            }
            // Uniform param-column width so the arrows line up.
            std::size_t name_w = 0;
            for (auto const& p : parameters) {
                std::size_t w = std::strlen(p.get_value().c_str());
                if (w > name_w) name_w = w;
            }
            std::size_t n = std::min(parameters.size(), arguments.size());
            for (std::size_t i = 0; i < n; ++i) {
                os << "    ";
                os << parameters[i].get_value().c_str();
                std::size_t pad = name_w - std::strlen(parameters[i].get_value().c_str());
                for (std::size_t k = 0; k < pad; ++k) os << ' ';
                os << " \xe2\x86\x90 ";  // ←
                print_token_container(os, arguments[i]);
                os << '\n';
            }
            if (parameters.size() > arguments.size()) {
                os << "    (missing " << (parameters.size() - arguments.size())
                   << " arg(s))\n";
            } else if (arguments.size() > parameters.size()) {
                os << "    (" << (arguments.size() - parameters.size())
                   << " extra arg(s) ignored)\n";
            }
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
                "  step / s                Step to next macro event (skip non-macro tokens)\n"
                "  step N / s N            Step forward exactly N preprocessing events (any kind)\n"
                "  next / n                Same as bare `step` (step to next macro event)\n"
                "  finish / fin            Run until the current (top-level) macro finishes\n"
                "  continue / c            Continue until breakpoint or end\n"
                "\n"
                "Breakpoints:\n"
                "  break [<type>] <macro> / b [<t>] <m>  Set breakpoint (type defaults to call)\n"
                "  delete [<type>] <macro> / d [<t>] <m> Remove breakpoint by name (type defaults to call)\n"
                "  delete <N> / d <N>                 Remove breakpoint by number\n"
                "  delete / d                         Remove all breakpoints\n"
                "  info breakpoints / i b             List breakpoints\n"
                "    types: call/c, expand/e, rescan/r, lex/l\n"
                "  info args / i a                    Show arg → param bindings at current call\n"
                "  info stack / i s                    Show all expansion frames with bound args\n"
                "  info disabled / i d                Show macros painted blue (per-rescan-scope disabled set)\n"
                "  info macro <name> / i M <name>     Show a macro's definition + call-stack depth\n"
                "\n"
                "Inspection:\n"
                "  list / l                 Show source code around current position\n"
                "  backtrace [N] / bt [N]   Show expansion stack (last N frames)\n"
                "  backtrace full / bt full Show expansion stack with bound args per frame\n"
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
            // Set a flag instead of throwing here: throwing from inside the
            // linenoise completion/callback would unwind across the C
            // `linenoise` call frame (undefined behavior, crashes on exit).
            // The prompt loop checks `want_quit` after each `parse()` and
            // breaks cleanly; `prompt()` then re-throws `session_terminate`
            // from a fully C++ call stack (no C frame in between), which
            // `ppstep.cpp`'s main loop catches to stop preprocessing.
            want_quit = true;
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
            if (st.expanding.empty() && st.rescanning.empty()) {
                std::cout << "No macro expansion in progress.\n" << std::flush;
                return;
            }
            // The "current working macro" is the top-level macro the frames-log
            // closer labels `working:` — the outermost live frame. Pick it the
            // same way the closer does: rescanning.front() when a rescan is
            // running (nested calls happen DURING the rescan, so the oldest
            // rescan frame is the true top-level), else expanding.front().
            std::string name = "<unknown>";
            if (!st.rescanning.empty()) {
                name = st.rescanning.front().first.empty()
                    ? "<empty>"
                    : std::string(st.rescanning.front().first.begin()->get_value().c_str());
            } else if (!st.expanding.empty()) {
                name = st.expanding.front().call.empty()
                    ? "<empty>"
                    : std::string(st.expanding.front().call.begin()->get_value().c_str());
            }
            std::cout << "Finishing expansion of: " << name
                      << "  (run until result)\n" << std::flush;
            cl.arm_finish();
            cl.set_mode(stepping_mode::UNTIL_BREAK);
            // Like `continue`, set steps_requested so the prompt loop breaks
            // out and preprocessing resumes. Without this, `finish` set the
            // mode but never returned to the main loop, so it just re-read
            // the next command and never actually ran.
            steps_requested = 1;
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
                auto print_frame = [&](char const* label, expansion_frame<ContainerT> const& frame) {
                    if (frame.call.empty()) {
                        std::cout << "  " << label << " : (empty frame)\n\n";
                        return;
                    }
                    std::string name(frame.call.begin()->get_value().c_str());
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

        // `info stack` / `i s`: every expansion frame with bound args at each
        // level — the gdb `bt full` analogue. Frame #0 is the innermost
        // (currently-expanding) call. Reuses `expanding_trace(full=true)`.
        template <class ContextT>
        void show_stack(ContextT& ctx) {
            auto const& st = cl.get_state();
            if (st.expanding.empty()) {
                std::cout << "Not currently inside any macro expansion.\n"
                          << std::flush;
                return;
            }
            std::cout << "Expansion stack (depth " << st.expanding.size()
                      << ", innermost first):\n";
            expanding_trace(ctx, -1, true);
        }

        // `info disabled` / `i d`: the "painted-blue" disabled set. A macro is
        // disabled for exactly the lifetime of its rescan frame (pushed in
        // `expanded_macro`, popped in `rescanned_macro`), so the disabled set
        // is the union of `cause` names across all live `rescanning` frames.
        // Innermost (current rescan) first; a macro expanding twice nested
        // shows once, at the deeper scope. Empty when no rescan is in progress.
        void show_disabled() {
            auto const& rescanning = cl.get_state().rescanning;
            if (rescanning.empty()) {
                std::cout << "No macros disabled (no rescan in progress).\n"
                          << std::flush;
                return;
            }
            // Union of cause names, innermost-first, dedup preserving
            // first-seen order (matches frames_pane_lines()).
            std::vector<std::string> names;
            for (auto it = rescanning.rbegin(); it != rescanning.rend(); ++it) {
                auto const& cause = it->first;
                if (cause.empty()) continue;
                std::string n(cause.begin()->get_value().c_str());
                if (std::find(names.begin(), names.end(), n) == names.end()) {
                    names.push_back(n);
                }
            }
            std::cout << "Disabled (painted blue), innermost-first:\n";
            for (std::size_t i = 0; i < names.size(); ++i) {
                std::cout << "  #" << i << "  " << names[i] << "\n";
            }
            std::cout << std::flush;
        }

        // `info macro NAME` / `i M NAME`: single-macro introspection. Looks
        // up the macro by name and pretty-prints its params, body, predefined
        // flag, and current expansion depth (how many frames on the call
        // stack are expanding this macro right now). Handles "macro not found"
        // cleanly. Promotes the internal lookup already used by
        // `print_verbose`/`print_args` to a first-class command.
        template <class ContextT, class Attr>
        void show_macro(ContextT& ctx, Attr const& attr) {
            std::string name(attr.begin(), attr.end());

            // wave's get_macro_definition does not throw for unknown names —
            // it returns an empty object-like definition. So check existence
            // explicitly first.
            if (!ctx.is_defined_macro(name)) {
                std::cout << "No macro named \"" << name << "\" is currently defined.\n"
                          << std::flush;
                return;
            }

            bool has_params = false, is_predefined = false;
            typename ContextT::position_type pos;
            std::vector<typename ContextT::token_type> parameters;
            typename ContextT::token_sequence_type definition;
            try {
                ctx.get_macro_definition(name, has_params, is_predefined, pos, parameters, definition);
            } catch (...) {
                std::cout << "No macro named \"" << name << "\" is currently defined.\n"
                          << std::flush;
                return;
            }

            std::cout << name;
            if (has_params) {
                std::cout << '(';
                for (std::size_t i = 0; i < parameters.size(); ++i) {
                    if (i) std::cout << ", ";
                    std::cout << parameters[i].get_value().c_str();
                }
                std::cout << ')';
            }
            if (is_predefined) std::cout << "   (predefined)";
            std::cout << '\n';

            if (has_params) {
                std::cout << "  params: ";
                for (std::size_t i = 0; i < parameters.size(); ++i) {
                    if (i) std::cout << ", ";
                    std::cout << parameters[i].get_value().c_str();
                }
                std::cout << '\n';
            }

            std::cout << "  body  : ";
            std::string body;
            for (auto const& t : definition) body += t.get_value().c_str();
            constexpr std::size_t max_body = 72;
            if (body.size() > max_body) body = body.substr(0, max_body - 3) + "...";
            std::cout << body << '\n';

            // Current expansion depth for this macro: count frames on the
            // call stack whose call tokens begin with this name.
            std::size_t depth = 0;
            for (auto const& frame : cl.get_state().expanding) {
                if (!frame.call.empty() &&
                    frame.call.begin()->get_value().c_str() == name) {
                    ++depth;
                }
            }
            std::cout << "  on call stack: " << depth << " frame"
                      << (depth == 1 ? "" : "s") << '\n';

            std::cout << std::flush;
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

            // Always mirror the current frames state to the log file so
            // the user can `tail -F` it in another terminal.
            cl.write_frames_log(ctx);
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

            // Highlight the current line — the top-level macro's call site.
            // `get_main_pos()` follows the OUTERMOST expansion: it stays
            // pinned to the source-level call that started the current
            // expansion tree across every nested call/expand/rescan within
            // it, so this highlights the top working macro's line, not the
            // innermost current-expanding macro's definition. Bright
            // magenta + bold + underline matches the `call` event highlight
            // (ansi::bright_magenta_fg) so the highlighted line reads as
            // "this is the macro call in flight".
            bool color = ppstep::color_enabled();
            for (int i = start; i < end; ++i) {
                bool is_current = (i == current - 1);
                char marker = is_current ? '>' : ' ';
                if (color && is_current) os << ansi::white_bg << ansi::black_fg << ansi::bold;
                os << "  " << marker << " "
                   << std::setw(num_width) << (i + 1) << " | "
                   << lines[i];
                if (color && is_current) os << ansi::reset;
                os << '\n';
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
                          | (lit("next") | lit("n"))[PPSTEP_ACTION(step_next())]
                          | lexeme[((lit("backtrace") | lit("bt")) >> +space >> lit("full"))[PPSTEP_ACTION(expanding_trace(ctx, -1, true))]]
                          | lexeme[((lit("backtrace") | lit("bt")) >> +space >> uint_ >> +space >> lit("full"))[PPSTEP_ACTION(expanding_trace(ctx, boost::fusion::at_c<1>(attr), true))]]
                          | lexeme[((lit("backtrace") | lit("bt")) >> +space >> uint_)[PPSTEP_ACTION(expanding_trace(ctx, boost::fusion::at_c<1>(attr)))]]
                          | lexeme[(lit("backtrace") | lit("bt"))[PPSTEP_ACTION(expanding_trace(ctx))]]
                          | lexeme[((lit("forwardtrace") | lit("ft")) >> +space >> uint_)[PPSTEP_ACTION(rescanning_trace(boost::fusion::at_c<1>(attr)))]]
                          | lexeme[(lit("forwardtrace") | lit("ft"))[PPSTEP_ACTION(rescanning_trace())]]
                          | lexeme[
                              (lit("break") | lit("b")) >> *space > (
                                    ((lit("call") | lit("c")) > +space > anything[PPSTEP_ACTION(add_breakpoint(attr, preprocessing_event_type::CALL))])
                                  | ((lit("expand") | lit("e")) > +space > anything[PPSTEP_ACTION(add_breakpoint(attr, preprocessing_event_type::EXPANDED))])
                                  | ((lit("rescan") | lit("r")) > +space > anything[PPSTEP_ACTION(add_breakpoint(attr, preprocessing_event_type::RESCANNED))])
                                  | ((lit("lex") | lit("l")) > +space > anything[PPSTEP_ACTION(add_breakpoint(attr, preprocessing_event_type::LEXED))])
                                  // Bare `b <macro>` defaults to a CALL breakpoint.
                                  | anything[PPSTEP_ACTION(add_breakpoint(attr, preprocessing_event_type::CALL))]
                            )]
                          | lexeme[
                              (lit("delete") | lit("d")) >> *space > (
                                    ((lit("call") | lit("c")) > +space > anything[PPSTEP_ACTION(remove_breakpoint(attr, preprocessing_event_type::CALL))])
                                  | ((lit("expand") | lit("e")) > +space > anything[PPSTEP_ACTION(remove_breakpoint(attr, preprocessing_event_type::EXPANDED))])
                                  | ((lit("rescan") | lit("r")) > +space > anything[PPSTEP_ACTION(remove_breakpoint(attr, preprocessing_event_type::RESCANNED))])
                                  | ((lit("lex") | lit("l")) > +space > anything[PPSTEP_ACTION(remove_breakpoint(attr, preprocessing_event_type::LEXED))])
                                  // Bare `d <macro>` defaults to a CALL breakpoint.
                                  | anything[PPSTEP_ACTION(remove_breakpoint(attr, preprocessing_event_type::CALL))]
                            )]
                          | lexeme[(lit("expand") | lit("e")) > +space > anything[PPSTEP_ACTION(expand_macro(ctx, attr))]]
                          | lexeme[lit("#define") > +space > anything[PPSTEP_ACTION(define_macro(ctx, attr))]]
                          | lexeme[lit("#undef") > +space > anything[PPSTEP_ACTION(undefine_macro(ctx, attr))]]
                          | lexeme[lit("#include") > +space > anything[PPSTEP_ACTION(include_file(ctx, attr))]]

                          | ((lit("info") | lit("i")) >> eoi)[PPSTEP_ACTION(show_info_summary(ctx))]
                          | lexeme[(lit("info") | lit("i")) >> +space > (
                                (lit("breakpoints") | lit("b"))[PPSTEP_ACTION(list_breakpoints())]
                              | (lit("stack") | lit("s"))[PPSTEP_ACTION(show_stack(ctx))]
                              | (lit("disabled") | lit("d"))[PPSTEP_ACTION(show_disabled())]
                              | (lit("macro") | lit("M")) >> +space > anything[PPSTEP_ACTION(show_macro(ctx, attr))]
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
                        {"next","next"}, {"n","next"},
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

                if (want_quit) {
                    // Re-throw from a C++-only call stack so the throw never
                    // crosses the C `linenoise` frame. See `quit()`.
                    throw session_terminate();
                }

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
        bool want_quit = false;
    };
}

#endif // PPSTEP_VIEW_HPP
