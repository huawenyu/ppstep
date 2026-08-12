#ifndef PPSTEP_CLIENT_HPP
#define PPSTEP_CLIENT_HPP

#include <vector>
#include <stack>
#include <optional>
#include <variant>
#include <tuple>
#include <iostream>
#include <sstream>
#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <set>
#include <functional>
#include <ctime>
#include <unistd.h>

#include "server_fwd.hpp"
#include "client_fwd.hpp"
#include "view.hpp"
#include "utils.hpp"

namespace ppstep {
    namespace ansi {
        // Foreground-only highlight palette. The highlight window marks a
        // token span (the current macro call / expansion / rescan) with a
        // bright foreground color + underline instead of a background fill.
        // Background-inverted cells fight the terminal theme: on a dark
        // theme the standard yellow bg ("\u001b[43m") renders as dark olive
        // and black text on it is low contrast.  Bright fg colors on the
        // terminal's own background stay vivid and readable on both light
        // and dark themes; underline marks the span boundary without
        // inverting the cell.
        constexpr auto bright_magenta_fg = "\u001b[95m";  // call
        constexpr auto bright_yellow_fg  = "\u001b[93m";  // expanded
        constexpr auto bright_cyan_fg    = "\u001b[96m";  // rescanned

        constexpr auto underline = "\u001b[4m";
        constexpr auto bold      = "\u001b[1m";

        constexpr auto reset = "\u001b[0m";
    }

    namespace events {
        template <class ContainerT, class DerivedT>
        struct formatting_event {
            formatting_event(std::size_t start, std::size_t end) : start(start), end(end) {}

            // How many tokens of surrounding context to show on each side of
            // the highlighted macro window. The full accumulated output is
            // not useful here — it grows monotonically and re-dumps the whole
            // file body at every stop. A small context window keeps the
            // display compact while still showing where the macro sits.
            static constexpr std::size_t context_radius = 8;

            void print(std::ostream& os, ContainerT const& tokens) const {
                if (tokens.empty()) { os << std::endl; return; }

                auto begin = tokens.begin();
                auto total_end = tokens.end();
                auto sub_start = std::next(begin, start);
                auto sub_end = std::next(begin, end);

                bool color = ppstep::color_enabled();

                // Clamp the leading/trailing context to context_radius tokens.
                auto ctx_begin = (start > context_radius)
                                 ? std::next(begin, start - context_radius)
                                 : begin;
                auto ctx_end = (static_cast<std::size_t>(std::distance(sub_end, total_end)) > context_radius)
                               ? std::next(sub_end, context_radius)
                               : total_end;

                if (color) os << ansi::bold;
                if (ctx_begin != sub_start) {
                    print_token_range(os, ctx_begin, sub_start);
                    os << ' ';
                }

                if (color) static_cast<DerivedT const*>(this)->format(os);
                if (sub_start != sub_end) {
                    print_token_range(os, sub_start, sub_end);
                    if (color) os << ansi::reset;
                } else {
                    os << ' ';
                    if (color) os << ansi::reset;
                }
                if (color) os << ansi::bold;
                if (sub_end != ctx_end)
                    os << ' ';

                print_token_range(os, sub_end, ctx_end);
                if (color) os << ansi::reset;
                os << std::endl;
            }

            std::size_t start, end;
        };

        template <class ContainerT>
        struct call : formatting_event<ContainerT, call<ContainerT>> {
            call(ContainerT tokens, std::size_t start, std::size_t end,
                 std::vector<ContainerT> arguments = {})
                : formatting_event<ContainerT, call<ContainerT>>(start, end),
                  tokens(std::move(tokens)),
                  arguments(std::move(arguments)) {}

            char const* event_kind() const { return "call"; }
            std::optional<std::string> event_macro_name() const {
                if (tokens.empty()) return std::nullopt;
                return std::string(tokens.begin()->get_value().c_str());
            }
            void print_summary(std::ostream& os) const {
                os << "  call     ";
                if (auto n = event_macro_name()) os << *n;
                else                                 os << "(unknown)";
                os << "\n";
            }

            void format(std::ostream& os) const {
                os << ansi::bright_magenta_fg << ansi::underline << ansi::bold;
            }

            void explain(std::ostream& os) const {
                os << "called macro " << ansi::bright_magenta_fg << ansi::underline << ansi::bold;
                print_token_container(os, tokens) << ansi::reset << std::endl;
            }

            // Verbose: also print "#define NAME(params) body" by looking up the
            // macro definition in the wave context.
            template <class ContextT>
            void print_verbose(std::ostream& os, ContainerT const& tokens, ContextT& ctx) const {
                this->print(os, tokens);
                if (this->tokens.empty()) { os << std::endl; return; }

                std::string name(this->tokens.begin()->get_value().c_str());

                bool has_params = false, is_predefined = false;
                typename ContextT::position_type pos;
                std::vector<typename ContextT::token_type> parameters;
                typename ContextT::token_sequence_type definition;

                try {
                    ctx.get_macro_definition(name, has_params, is_predefined, pos, parameters, definition);

                    // Render the definition in a small box so the LHS
                    // (`#define NAME(params)`) is visually separated from
                    // the body — particularly helpful when the body is long.
                    os << "\n  ┌── #define " << name;
                    if (has_params) {
                        os << '(';
                        for (std::size_t i = 0; i < parameters.size(); ++i) {
                            if (i) os << ", ";
                            os << parameters[i].get_value().c_str();
                        }
                        os << ')';
                    }
                    os << " ";
                    if (parameters.size() + 1 > 0) {
                        // visual separator before the body
                        os << "\xe2\x94\x82";  // │
                    }
                    os << '\n';

                    if (has_params) {
                        os << "  │   params : ";
                        for (std::size_t i = 0; i < parameters.size(); ++i) {
                            if (i) os << ", ";
                            os << parameters[i].get_value().c_str();
                        }
                        os << '\n';
                    }

                    // Body on its own line. If too long, wrap.
                    std::string body;
                    for (auto const& tok : definition) {
                        body += tok.get_value().c_str();
                    }
                    constexpr std::size_t max_body_width = 60;
                    os << "  │   body   : ";
                    std::size_t col = 0;
                    for (char c : body) {
                        os << c;
                        ++col;
                        if (c == '\n' || (col >= max_body_width && std::isspace(static_cast<unsigned char>(c)))) {
                            os << "\n  │             ";
                            col = 0;
                        }
                    }
                    os << '\n';
                    os << "  └──";
                    if (is_predefined) os << "  (predefined)";
                } catch (...) {
                    os << "\n  └── #define " << name << "  (lookup failed)";
                }
                os << std::endl;
            }

            // `info args`: print formal parameters and the argument tokens
            // that were substituted for them at this call site. Object-like
            // macros have no arguments; we still print the macro name.
            template <class ContextT>
            void print_args(std::ostream& os, ContextT& ctx) const {
                if (this->tokens.empty()) {
                    os << "(no macro name)\n";
                    return;
                }
                std::string name(this->tokens.begin()->get_value().c_str());

                bool has_params = false, is_predefined = false;
                typename ContextT::position_type pos;
                std::vector<typename ContextT::token_type> parameters;
                typename ContextT::token_sequence_type definition;
                try {
                    ctx.get_macro_definition(name, has_params, is_predefined, pos, parameters, definition);
                } catch (...) {
                    os << "(unknown macro: " << name << ")\n";
                    return;
                }

                if (!has_params) {
                    os << name << " is object-like — no arguments.\n";
                    return;
                }

                os << "args of " << name;
                if (is_predefined) os << "  (predefined)";
                os << ":\n";

                // Compute uniform param-column width so the arrows line up.
                std::size_t name_w = 0;
                for (auto const& p : parameters) {
                    std::size_t w = std::strlen(p.get_value().c_str());
                    if (w > name_w) name_w = w;
                }

                std::size_t n = std::min(parameters.size(), arguments.size());
                for (std::size_t i = 0; i < n; ++i) {
                    os << "  " << parameters[i].get_value().c_str();
                    std::size_t pad = name_w - std::strlen(parameters[i].get_value().c_str());
                    for (std::size_t k = 0; k < pad; ++k) os << ' ';
                    os << " \xe2\x86\x90 ";
                    print_token_container(os, arguments[i]);
                    os << "\n";
                }
                if (parameters.size() > arguments.size()) {
                    os << "  (missing " << (parameters.size() - arguments.size())
                       << " arg(s))\n";
                } else if (arguments.size() > parameters.size()) {
                    os << "  (" << (arguments.size() - parameters.size())
                       << " extra arg(s) ignored)\n";
                }
            }

            ContainerT tokens;
            std::vector<ContainerT> arguments;
        };

        template <class ContainerT>
        struct expanded : formatting_event<ContainerT, expanded<ContainerT>> {
            expanded(ContainerT initial, std::size_t start, std::size_t end)
                : formatting_event<ContainerT, expanded<ContainerT>>(start, end), initial(std::move(initial)) {}

            void format(std::ostream& os) const {
                os << ansi::bright_yellow_fg << ansi::underline << ansi::bold;
            }

            void explain(std::ostream& os) const {
                os << "expanded macro " << ansi::bright_yellow_fg << ansi::underline << ansi::bold;
                print_token_container(os, initial) << ansi::reset << std::endl;
            }

            template <class ContextT>
            void print_verbose(std::ostream& os, ContainerT const& tokens, ContextT&) const {
                this->print(os, tokens);
                os << "  ── expanded from: ";
                print_token_container(os, initial);
                os << std::endl;
            }

            template <class ContextT>
            void print_args(std::ostream& os, ContextT&) const {
                os << "expanded — no args (already substituted into the body).\n" << std::flush;
            }

            char const* event_kind() const { return "expanded"; }
            std::optional<std::string> event_macro_name() const {
                if (initial.empty()) return std::nullopt;
                return std::string(initial.begin()->get_value().c_str());
            }
            void print_summary(std::ostream& os) const {
                os << "  expanded ";
                if (auto n = event_macro_name()) os << *n;
                else                                 os << "(unknown)";
                os << "\n";
            }

            ContainerT initial;
        };

        template <class ContainerT>
        struct rescanned : formatting_event<ContainerT, rescanned<ContainerT>> {
            rescanned(ContainerT cause, ContainerT initial, std::size_t start, std::size_t end, ContainerT result = {})
                : formatting_event<ContainerT, rescanned<ContainerT>>(start, end), cause(std::move(cause)), initial(std::move(initial)), result(std::move(result)) {}

            void format(std::ostream& os) const {
                os << ansi::bright_cyan_fg << ansi::underline << ansi::bold;
            }

            void explain(std::ostream& os) const {
                os << "rescanned macro " << ansi::bright_yellow_fg << ansi::underline << ansi::bold;
                print_token_container(os, initial) << ansi::reset << "\ncaused by " << ansi::bright_magenta_fg << ansi::underline << ansi::bold;
                print_token_container(os, cause) << ansi::reset << std::endl;
            }

            template <class ContextT>
            void print_verbose(std::ostream& os, ContainerT const& tokens, ContextT&) const {
                this->print(os, tokens);
                os << "  ── rescan caused by: ";
                print_token_container(os, cause);
                os << "  (over: ";
                print_token_container(os, initial);
                os << ')' << std::endl;
            }

            template <class ContextT>
            void print_args(std::ostream& os, ContextT&) const {
                os << "rescanned — no args.\n" << std::flush;
            }

            char const* event_kind() const { return "rescanned"; }
            std::optional<std::string> event_macro_name() const {
                if (cause.empty()) return std::nullopt;
                return std::string(cause.begin()->get_value().c_str());
            }
            void print_summary(std::ostream& os) const {
                os << "  rescanned    cause=";
                if (auto n = event_macro_name()) os << *n;
                else                                 os << "(unknown)";
                os << "\n";
            }

            ContainerT cause, initial, result;
        };

        template <class ContainerT>
        struct lexed {
            void print(std::ostream& os, ContainerT const& tokens) const {
                // Only print the newly-lexed token (the last one), not the
                // entire accumulated output — otherwise every `step` at a
                // LEXED event re-dumps the whole file body so far.
                if (tokens.empty()) return;
                if (ppstep::color_enabled()) os << ansi::bold;
                print_token(os, tokens.back());
                if (ppstep::color_enabled()) os << ansi::reset;
                os << std::endl;
            }

            template <class ContextT>
            void print_verbose(std::ostream& os, ContainerT const& tokens, ContextT&) const {
                this->print(os, tokens);
            }

            template <class ContextT>
            void print_args(std::ostream& os, ContextT&) const {
                os << "lexed — no args (single token, not a call).\n" << std::flush;
            }

            char const* event_kind() const { return "lexed"; }
            std::optional<std::string> event_macro_name() const { return std::nullopt; }
            void print_summary(std::ostream& os) const {
                os << "  lexed\n";
            }

            void explain(std::ostream& os) const {
                os << "lexed tokens ?" << std::endl;
            }
        };
    }
    
    template <class ContainerT>
    using preprocessing_event =
        std::variant<
            events::call<ContainerT>,
            events::expanded<ContainerT>,
            events::rescanned<ContainerT>,
            events::lexed<ContainerT>>;
    
    template <class ContainerT>
    struct offset_container {
        using iterator = typename ContainerT::const_iterator;
        
        offset_container(ContainerT&& tokens, iterator&& start) : tokens(std::move(tokens)), start(std::move(start)) {}
        
        offset_container(ContainerT&& tokens) : tokens(std::move(tokens)), start(this->tokens.end()) {}
        
        offset_container(offset_container<ContainerT> const&) = delete;
        
        std::optional<std::pair<iterator, iterator>> find_pattern(ContainerT const& pattern) const {
            return find_sublist(tokens, pattern, start);
        }
        
        ContainerT tokens;
        iterator start;
    };
    
    template <class ContainerT>
    struct historical_event {
        historical_event(ContainerT tokens, preprocessing_event<ContainerT>&& event) : tokens(std::move(tokens)), event(std::move(event)) {}

        ContainerT tokens;
        preprocessing_event<ContainerT> event;
    };
    
    template <class TokenT, class ContainerT>
    struct client {
        client(server_state<ContainerT>& state, std::string prefix) : state(&state), cli(client_cli<TokenT, ContainerT>(*this, std::move(prefix))), mode(stepping_mode::FREE) {}
        
        client(server_state<ContainerT>& state) : client(state, "") {}

        template <class ContextT>
        void on_lexed(ContextT& ctx, TokenT const& token) {
            if (token_stack.empty()) {
                auto last_tokens = token_history.empty() ? ContainerT() : newest_history()->tokens;
                last_tokens.push_back(token);

                lexed_tokens.push_back(token);
                token_history.push_back(historical_event<ContainerT>(last_tokens, events::lexed<ContainerT>()));

                handle_prompt(ctx, token, preprocessing_event_type::LEXED);

            } else {
                auto const& last_tokens = newest_history()->tokens;

                lex_buffer.push_back(token);
                if (std::equal(std::next(std::begin(last_tokens), lexed_tokens.size()), std::end(last_tokens),
                               std::begin(lex_buffer), std::end(lex_buffer),
                               [](auto const& a, auto const& b) { return a.get_value() == b.get_value(); })) {
                    lexed_tokens.insert(std::end(lexed_tokens), std::begin(lex_buffer), std::end(lex_buffer));
                    lex_buffer.clear();
                    reset_token_stack();
                }
            }
        }

        template <class ContextT>
        void on_expand_function(ContextT& ctx, TokenT const& call, std::vector<ContainerT> const& arguments, ContainerT call_tokens) {
            if (token_stack.empty()) {
                push(std::move(call_tokens), events::call<ContainerT>(call_tokens, lexed_tokens.size() + 0, lexed_tokens.size() + call_tokens.size(), arguments));
            } else {
                auto lookup = find_match_indices(token_stack.back(), call_tokens);
                if (lookup) {
                    auto [start, end] = *lookup;
                    token_history.push_back(historical_event<ContainerT>(
                        prepend_lexed(token_stack.back().tokens),
                        events::call<ContainerT>(call_tokens, lexed_tokens.size() + start, lexed_tokens.size() + end, arguments)));
                } else {
                    reset_token_stack();
                    push(std::move(call_tokens), events::call<ContainerT>(call_tokens, lexed_tokens.size() + 0, lexed_tokens.size() + call_tokens.size(), arguments));
                }
            }

            handle_prompt(ctx, call, preprocessing_event_type::CALL);
        }

        template <class ContextT>
        void on_expand_object(ContextT& ctx, TokenT const& call) {
            auto call_tokens = ContainerT{call};
            
            if (token_stack.empty()) {
                push(std::move(call_tokens), events::call<ContainerT>(call_tokens, lexed_tokens.size() + 0, lexed_tokens.size() + call_tokens.size()));
            } else {
                auto lookup = find_match_indices(token_stack.back(), call_tokens);
                if (lookup) {
                    auto [start, end] = *lookup;
                    token_history.push_back(historical_event<ContainerT>(
                        prepend_lexed(token_stack.back().tokens),
                        events::call<ContainerT>(call_tokens, lexed_tokens.size() + start, lexed_tokens.size() + end)));
                } else {
                    reset_token_stack();
                    push(std::move(call_tokens), events::call<ContainerT>(call_tokens, lexed_tokens.size() + 0, lexed_tokens.size() + call_tokens.size()));
                }
            }

            handle_prompt(ctx, call, preprocessing_event_type::CALL);
        }

        template <class ContextT>
        void on_expanded(ContextT& ctx, ContainerT const& initial, ContainerT const& result) {
            try {
                auto const& [tokens, start, end] = match(initial);

                ContainerT new_tokens;
                std::size_t new_start, new_end;
                splice_between(*tokens, result, start, end, new_tokens, new_start, new_end);

                push(std::move(new_tokens),
                     std::next(new_tokens.begin(), new_start),
                     events::expanded<ContainerT>(initial, lexed_tokens.size() + new_start, lexed_tokens.size() + new_end));

            } catch (std::logic_error const&) {
                push(ContainerT(result), events::expanded<ContainerT>(initial, lexed_tokens.size() + 0, lexed_tokens.size() + result.size()));
            }

            handle_prompt(ctx, *(initial.begin()), preprocessing_event_type::EXPANDED);
        }

        template <class ContextT>
        void on_rescanned(ContextT& ctx, ContainerT const& cause, ContainerT const& initial, ContainerT const& result) {
            if (initial.empty()) return;

            try {
                auto const& [tokens, start, end] = match(initial);

                ContainerT new_tokens;
                std::size_t new_start, new_end;
                splice_between(*tokens, result, start, end, new_tokens, new_start, new_end);
                
                push(std::move(new_tokens),
                     std::next(new_tokens.begin(), new_start),
                     events::rescanned<ContainerT>(cause, initial, lexed_tokens.size() + new_start, lexed_tokens.size() + new_end, result));

            } catch (std::logic_error const&) {
                push(ContainerT(result), events::rescanned<ContainerT>(cause, initial, lexed_tokens.size() + 0, lexed_tokens.size() + result.size(), result));
            }

            // A `rescanned` that leaves BOTH the call stack and the rescan
            // queue empty marks the completion of a whole top-level macro
            // expansion tree — its result is final. Stash it BEFORE firing
            // handle_prompt so the prompt (and the frames-log closer it
            // renders via current_state → write_frames_log) sees `done:` /
            // `(result)` at this stop, not `working:`. The server fires this
            // hook BEFORE popping the rescanning frame (server.hpp:146 then
            // :153), so at this instant the about-to-pop frame is still on the
            // queue — a completion is `rescanning.size()==1` (only the current
            // frame left) AND `expanding` already empty.
            if (state->expanding.empty() && state->rescanning.size() == 1) {
                last_completed_cause.clear();
                last_completed_call.clear();
                last_completed_result.clear();
                // Render the full original call (name + args), e.g. `BOOL(123)`,
                // not just the bare name — the closer/done line should show the
                // source-level invocation that produced this result.
                for (auto const& tk : cause) {
                    if (!tk.is_valid()) continue;
                    if (IS_CATEGORY(tk, boost::wave::WhiteSpaceTokenType)) continue;
                    if (boost::wave::token_id(tk) == boost::wave::T_PLACEMARKER) continue;
                    auto v = tk.get_value().c_str();
                    if (!last_completed_call.empty()
                        && last_completed_call.back() != '('
                        && v[0] != ')' && v[0] != '(') {
                        last_completed_call += ' ';
                    }
                    last_completed_call += v;
                }
                if (!cause.empty()) {
                    last_completed_cause = cause.begin()->get_value().c_str();
                }
                for (auto const& tk : result) {
                    if (tk.is_valid()
                        && !IS_CATEGORY(tk, boost::wave::WhiteSpaceTokenType)
                        && boost::wave::token_id(tk) != boost::wave::T_PLACEMARKER) {
                        last_completed_result += tk.get_value().c_str();
                        last_completed_result += ' ';
                    }
                }
                if (!last_completed_result.empty()
                    && last_completed_result.back() == ' ') {
                    last_completed_result.pop_back();
                }
            }

            // Use `cause` (the macro whose body was just rescanned) for the
            // prompt token — `initial` here is the rescan input, whose first
            // token may be a parenthesis or a literal, not the macro name.
            handle_prompt(ctx, *(cause.begin()), preprocessing_event_type::RESCANNED);
        }
        
        template <typename ContextT, typename ExceptionT>
        void on_exception(ContextT& ctx, ExceptionT const& e) {
            // Recoverable exceptions (missing #include resolved only from
            // explicit -I, ill-formed #if from unknown builtins, etc.) are
            // already warned-and-skipped inside server::throw_exception —
            // prompting here would consume the `steps_requested`/`UNTIL_BREAK`
            // state from an in-flight `continue`, so `continue` silently
            // drops to FREE mode and never stops at the breakpoint.  Only
            // prompt for truly fatal exceptions (the ones about to be
            // re-thrown), where this is the last chance to inspect state.
            if (e.is_recoverable()) return;
            cli.prompt(ctx, "exception");
        }

        template <class ContextT>
        void on_complete(ContextT& ctx) {
            cli.prompt(ctx, "complete");
        }
        
        template <class ContextT>
        void on_start(ContextT& ctx) {
            std::cout << "Preprocessing " << ctx.get_main_pos() << '.' << std::endl;
            cli.prompt(ctx, "started", false);
        }

        void add_breakpoint(typename TokenT::string_type const& macro, preprocessing_event_type cond) {
            switch (cond) {
                case preprocessing_event_type::CALL: {
                    if (expansion_breakpoints.insert(macro).second)
                        numbered_breakpoints.push_back({next_bp_id++, macro, cond});
                    break;
                }
                case preprocessing_event_type::EXPANDED: {
                    if (expanded_breakpoints.insert(macro).second)
                        numbered_breakpoints.push_back({next_bp_id++, macro, cond});
                    break;
                }
                case preprocessing_event_type::RESCANNED: {
                    if (rescanned_breakpoints.insert(macro).second)
                        numbered_breakpoints.push_back({next_bp_id++, macro, cond});
                    break;
                }
                case preprocessing_event_type::LEXED: {
                    if (lexed_breakpoints.insert(macro).second)
                        numbered_breakpoints.push_back({next_bp_id++, macro, cond});
                    break;
                }
                default: break;
            }
        }

        void remove_breakpoint(typename TokenT::string_type const& macro, preprocessing_event_type cond) {
            switch (cond) {
                case preprocessing_event_type::CALL: {
                    expansion_breakpoints.erase(macro);
                    break;
                }
                case preprocessing_event_type::EXPANDED: {
                    expanded_breakpoints.erase(macro);
                    break;
                }
                case preprocessing_event_type::RESCANNED: {
                    rescanned_breakpoints.erase(macro);
                    break;
                }
                case preprocessing_event_type::LEXED: {
                    lexed_breakpoints.erase(macro);
                    break;
                }
                default: break;
            }
            numbered_breakpoints.erase(
                std::remove_if(numbered_breakpoints.begin(), numbered_breakpoints.end(),
                    [&macro, cond](auto const& bp) { return bp.name == macro && bp.type == cond; }),
                numbered_breakpoints.end());
        }

        bool remove_breakpoint(int id) {
            auto it = std::find_if(numbered_breakpoints.begin(), numbered_breakpoints.end(),
                [id](auto const& bp) { return bp.id == id; });
            if (it == numbered_breakpoints.end()) return false;

            switch (it->type) {
                case preprocessing_event_type::CALL: expansion_breakpoints.erase(it->name); break;
                case preprocessing_event_type::EXPANDED: expanded_breakpoints.erase(it->name); break;
                case preprocessing_event_type::RESCANNED: rescanned_breakpoints.erase(it->name); break;
                case preprocessing_event_type::LEXED: lexed_breakpoints.erase(it->name); break;
                default: break;
            }
            numbered_breakpoints.erase(it);
            return true;
        }

        struct numbered_bp {
            int id;
            typename TokenT::string_type name;
            preprocessing_event_type type;
        };

        // `finish` records the depth to RETURN TO once the current macro
        // `finish`: run until the current top-level working macro — the one
        // the frames-log closer labels `working:` — completes its ENTIRE
        // expansion tree. That completion point is exactly the condition
        // `on_rescanned` (line 541) already detects: both the call stack
        // (`expanding`) and the rescan queue (`rescanning`) drained, i.e.
        // `expanding.empty() && rescanning.size() == 1` at a RESCANNED event
        // (the about-to-pop last frame still counted, since the server pops
        // AFTER firing the hook — server.hpp:146 then :153). So `finish` just
        // arms a flag and lets UNTIL_BREAK run every event until that RESCANNED
        // fires; it stops there and the closer renders the `(result)` line.
        // Issued when BOTH stacks are already empty (nothing in flight), bail.
        void arm_finish() {
            if (state->expanding.empty() && state->rescanning.empty()) return;
            finish_pending = true;
        }
        void disarm_finish() { finish_pending = false; }
        bool is_finish_armed() const { return finish_pending; }

        std::vector<numbered_bp> const& list_breakpoints() const {
            return numbered_breakpoints;
        }

        char const* bp_type_name(preprocessing_event_type t) const {
            switch (t) {
                case preprocessing_event_type::CALL: return "call";
                case preprocessing_event_type::EXPANDED: return "expand";
                case preprocessing_event_type::RESCANNED: return "rescan";
                case preprocessing_event_type::LEXED: return "lex";
                default: return "?";
            }
        }
        
        server_state<ContainerT> const& get_state() {
            return *state;
        }

        void set_mode(stepping_mode m) {
            mode = m;
        }

        void set_verbose(bool on) { verbose = on; }
        bool is_verbose() const { return verbose; }

        auto newest_history() {
            return token_history.rbegin();
        }
        
        auto oldest_history() {
            return token_history.rend();
        }

    private:
        using container_iterator = typename ContainerT::const_iterator;
        
        using range_container = std::tuple<ContainerT const*, container_iterator, container_iterator>;

        ContainerT prepend_lexed(ContainerT const& tokens) {
            auto acc = ContainerT(std::begin(lexed_tokens), std::end(lexed_tokens));
            acc.insert(std::end(acc), std::begin(tokens), std::end(tokens));
            return acc;
        }

        void push(ContainerT&& tokens, preprocessing_event<ContainerT>&& event) {
            push(std::move(tokens), std::begin(tokens), std::move(event));
        }

        void push(ContainerT&& tokens, container_iterator&& head, preprocessing_event<ContainerT>&& event) {
            auto historical_tokens = prepend_lexed(tokens);
            token_history.push_back(historical_event<ContainerT>(historical_tokens, std::move(event)));

            if (head != tokens.end()) {
                token_stack.emplace_back(std::move(tokens), std::move(head));
            } else {
                token_stack.emplace_back(std::move(tokens));
            }
        }

        range_container match(ContainerT const& pattern) {
            while (!token_stack.empty()) {
                auto const& top = token_stack.back();

                auto sublist = top.find_pattern(pattern);

                if (sublist) {
                    auto [start, end] = *sublist;

                    return std::make_tuple(&(top.tokens), start, end);
                } else {
                    token_stack.pop_back();
                }
            }
            
            std::stringstream ss;
            print_token_container(ss, pattern);
            throw std::logic_error("could not find pattern \"" + ss.str() + "\" in token stack");
        }
        
        std::optional<std::pair<std::size_t, std::size_t>> find_match_indices(offset_container<ContainerT> const& oc, ContainerT const& pattern) {
            auto sublist = oc.find_pattern(pattern);
            if (sublist) {
                auto [start, end] = *sublist;

                auto begin_to_start = std::distance(oc.tokens.begin(), start);
                auto begin_to_end = begin_to_start + std::distance(start, end);
                return {{begin_to_start, begin_to_end}};
            } else {
                return {};
            }
        }

        void splice_between(ContainerT const& tokens, ContainerT const& result, container_iterator start, container_iterator end,
                                                       ContainerT& new_tokens, std::size_t& new_start, std::size_t& new_end) {
            new_tokens.insert(new_tokens.end(), tokens.begin(), start);
            new_start = new_tokens.size();

            new_tokens.insert(new_tokens.end(), result.begin(), result.end());
            new_end = new_tokens.size();

            new_tokens.insert(new_tokens.end(), end, tokens.end());
        }

        void reset_token_stack() {
            token_stack.clear();
        }
        
        // Prompt trigger string. Verbs match the timing of each event:
        //   `calling X`     – about to substitute X (entry, frame being entered)
        //   `expanded X`    – just finished substituting X
        //   `rescanned X`   – just finished re-parsing the body
        //   `lexed X`       – just emitted token X
        std::string make_trigger(preprocessing_event_type type, TokenT const& token) const {
            std::string raw(token.get_value().c_str());
            // Newlines / tabs / other control chars in a token value (e.g.
            // the newline token at the end of a `#define` body or source
            // line) would corrupt the linenoise prompt line.  Render them
            // as visible escapes so the trigger stays single-line.
            std::string name;
            name.reserve(raw.size());
            for (char c : raw) {
                switch (c) {
                    case '\n': name += "\\n"; break;
                    case '\r': name += "\\r"; break;
                    case '\t': name += "\\t"; break;
                    default: name += c; break;
                }
            }
            if (name.empty()) name = "<eof>";
            switch (type) {
                case preprocessing_event_type::CALL:
                    return "calling " + name;
                case preprocessing_event_type::EXPANDED:
                    return "expanded " + name;
                case preprocessing_event_type::RESCANNED:
                    return "rescanned " + name;
                case preprocessing_event_type::LEXED:
                    return "lexed " + name;
                default:
                    return "";
            }
        }

    public:
        // Banner printed ABOVE the prompt line for `rescanned` events. Tells
        // the user where wave is heading next (rescan queue depth) AND where
        // they are in the expansion callstack (backtrace, innermost first).
        std::string make_rescanned_banner(std::string const& just_rescanned) const {
            std::ostringstream os;
            os << "\n";

            std::size_t qdepth = state->rescanning.size() == 0 ? 0 : state->rescanning.size() - 1;
            std::size_t edepth = state->expanding.size();

            // The widest key is "backtrace " — pad all keys to 9 chars.
            // The widest interior content here is ~50 chars; pick 52 for
            // border width so the box looks tight.
            constexpr int KEY_W = 9;
            constexpr int BOX_W = 52;

            std::string top_border = "  \xe2\x94\x8c\xe2\x94\x80\xe2\x94\x80 rescanned " + just_rescanned;
            // pad with light horizontal box-drawing to BOX_W - 1
            while ((int)top_border.size() < BOX_W) top_border += "\xe2\x94\x80";  // ─
            top_border += "\xe2\x94\x90\n";  // ┐
            os << top_border;

            // queue line — count + each pending frame listed (innermost
            // rescan first, since wave pops from the back).
            os << "  \xe2\x94\x82  " << std::setw(KEY_W) << std::left << "rescan q";
            os << ": ";
            if (qdepth == 0) {
                os << "empty (lex/punct events continue)";
            } else {
                os << qdepth << " frame" << (qdepth == 1 ? "" : "s")
                   << " pending (innermost first):";
            }
            os << "\n";
            if (qdepth > 0) {
                int qidx = 0;
                // Wave pops the most-recently-pushed frame from the back of
                // the queue. Skip rbegin() (the about-to-pop frame) and show
                // the remaining qdepth items — innermost-pushed-remaining
                // first, oldest pending last.
                auto it = state->rescanning.rbegin();
                ++it;  // skip the frame that this `rescanned` event is popping
                for (; qidx < (int)qdepth; ++qidx, ++it) {
                    auto const& cause = it->first;
                    std::string qname = cause.empty() ? "(unknown)"
                                                      : std::string(cause.begin()->get_value().c_str());
                    os << "  \xe2\x94\x82    #" << qidx << "  " << qname;
                    if (qidx == 0) os << "  \xe2\x97\x80 next to rescan";
                    os << "\n";
                }
            }

            // backtrace header
            os << "  \xe2\x94\x82  " << std::setw(KEY_W) << std::left << "call stack";
            os << ": ";
            if (edepth == 0) {
                os << "(empty)";
            } else {
                os << edepth << " frame" << (edepth == 1 ? "" : "s") << " (innermost first):";
            }
            os << "\n";

            // backtrace frames — 4 spaces of indent inside the box. No
            // padding past the macro name so trailing whitespace stays
            // tidy on lines without an annotation.
            int idx = 0;
            for (auto it = state->expanding.rbegin(); it != state->expanding.rend(); ++it, ++idx) {
                if (it->call.empty()) continue;
                std::string name(it->call.begin()->get_value().c_str());
                os << "  \xe2\x94\x82    #" << idx << "  " << name;
                if (idx == 0) {
                    // The innermost (#0) is always the current call frame.
                    os << "  \xe2\x97\x80 current frame";
                }
                os << "\n";
            }

            os << "  \xe2\x94\x94";
            for (int i = 0; i < BOX_W - 2; ++i) os << "\xe2\x94\x80";  // ─
            os << "\xe2\x94\x98\n";  // ┘
            return os.str();
        }

        // Left-pane content (rescan queue + call stack) plus per-row
        // metadata so the caller can (a) place each frame's `#define` in
        // the right pane on the SAME row as the frame's `#N NAME` line,
        // and (b) diff-highlight only genuinely-new frames.
        struct frames_pane {
            std::vector<std::string> lines;
            // For each call-stack frame row: its index into `lines`, and
            // the macro name on that row. Parallel vectors; rendered
            // innermost-first (matches the `#0` ordering).
            std::vector<std::size_t> frame_rows;
            std::vector<std::string> frame_names;
            // Same idea for rescan-queue frames: which left row each
            // `#N NAME` sits on, and its macro name, so the right pane
            // can render its `#define` on the same row.
            std::vector<std::size_t> rescan_frame_rows;
            std::vector<std::string> rescan_frame_names;
        };

        frames_pane frames_pane_lines() const {
            frames_pane fp;
            auto& lines = fp.lines;

            // ─── rescan queue ───
            // Render ALL live rescan frames innermost-first (#0 = the body
            // currently being rescanned). The earlier `size() - 1` hid the
            // innermost frame, so the queue read as "empty" whenever a rescan
            // was in progress — the exact moment the queue is most relevant.
            // `rescanned_macro` pops the frame before the prompt fires, so a
            // just-finished rescan is already gone (no double-count).
            lines.push_back("-- rescan queue --");
            std::size_t qdepth = state->rescanning.size();
            if (qdepth == 0) {
                lines.push_back("  empty (lex/punct next)");
            } else {
                int qidx = 0;
                for (auto it = state->rescanning.rbegin(); it != state->rescanning.rend(); ++it, ++qidx) {
                    auto const& cause = it->first;
                    std::string qname = cause.empty() ? "(unknown)"
                                                      : std::string(cause.begin()->get_value().c_str());
                    std::string line = "    #" + std::to_string(qidx) + " " + qname;
                    // ASCII marker "<-" instead of ◀ so byte-pad in
                    // write_frames_log stays aligned with char columns.
                    // #0 is the body being rescanned RIGHT NOW; deeper entries
                    // are pending (will be rescanned after this one completes).
                    if (qidx == 0) line += "  <- now";
                    else if (qidx == 1) line += "  <- next";
                    fp.rescan_frame_rows.push_back(lines.size());
                    fp.rescan_frame_names.push_back(qname);
                    lines.push_back(line);
                }
            }

            lines.push_back("");

            // ─── call stack ───
            lines.push_back("-- call stack --");
            std::size_t edepth = state->expanding.size();
            if (edepth == 0) {
                lines.push_back("  (empty)");
            } else {
                int eidx = 0;
                for (auto it = state->expanding.rbegin(); it != state->expanding.rend(); ++it, ++eidx) {
                    if (it->call.empty()) continue;
                    std::string ename(it->call.begin()->get_value().c_str());
                    std::string line = "    #" + std::to_string(eidx) + " " + ename;
                    // ASCII marker "<-" instead of ◀ for column alignment.
                    if (eidx == 0) line += "  <- current";
                    fp.frame_rows.push_back(lines.size());
                    fp.frame_names.push_back(ename);
                    lines.push_back(line);
                }
            }

            lines.push_back("");

            // ─── disabled (blue) ───
            // A macro is "painted blue" (disabled) for exactly the lifetime of
            // its `rescanning` frame — `expanded_macro` pushes the frame, the
            // macro's name is the frame's `cause` (`.first`), and
            // `rescanned_macro` pops it. So the disabled set IS the union of
            // `cause` names across all live `rescanning` frames. No separate
            // server state needed; derived from the same LIFO that mirrors
            // Wave's rescan stack, so it can't desync.
            lines.push_back("-- disabled (blue) --");
            if (state->rescanning.empty()) {
                lines.push_back("  (none)");
            } else {
                // Union of cause names, innermost-first, dedup preserving
                // first-seen order (a macro expanding twice nested shows once,
                // at the deeper scope).
                std::vector<std::string> names;
                for (auto it = state->rescanning.rbegin(); it != state->rescanning.rend(); ++it) {
                    auto const& cause = it->first;
                    if (cause.empty()) continue;
                    std::string n(cause.begin()->get_value().c_str());
                    if (std::find(names.begin(), names.end(), n) == names.end()) {
                        names.push_back(n);
                    }
                }
                std::string joined;
                for (std::size_t i = 0; i < names.size(); ++i) {
                    if (i) joined += ", ";
                    joined += names[i];
                }
                lines.push_back("  " + joined);
            }
            return fp;
        }

        // Path of the frames log file. Fixed at /tmp/ppstep_frames.log so
        // the user has a stable known location to `tail -f` from another
        // terminal. Appended-to on every event stop (file only grows),
        // so `tail -f` works without "file truncated" warnings.
        static std::string default_frames_log_path() {
            return "/tmp/ppstep_frames.log";
        }

        // Append a two-pane snapshot of the current preprocessing state to
        // the log file. Left pane: rescan queue + call stack. Right pane:
        // `#define` of the next macro wave will rescan, plus `#define` of
        // the macro wave is currently expanding. Appended to (never
        // truncated) so `tail -f` runs cleanly without "file truncated"
        // warnings or content overlap.
        template <class ContextT>
        void write_frames_log(ContextT& ctx) const {
            if (!frames_log_open) {
                std::string path = frames_log_path.empty() ? default_frames_log_path()
                                                           : frames_log_path;
                frames_log_file.open(path, std::ios::out | std::ios::app);
                frames_log_open = true;
            }
            if (!frames_log_file) return;

            char ts[32];
            std::time_t t = std::time(nullptr);
            std::tm tm_buf{};
            localtime_r(&t, &tm_buf);
            std::strftime(ts, sizeof(ts), "%H:%M:%S", &tm_buf);

            // -- LEFT pane + per-row frame metadata -----------------------
            frames_pane fp = frames_pane_lines();
            auto const& left_lines = fp.lines;

            // -- RIGHT pane: #define on the SAME row as the frame ---------
            // One line per left row; non-frame rows are blank so each
            // frame's `#define NAME ...` sits beside its `#N NAME` row in
            // the left pane, not in a separate top-aligned block.
            std::vector<std::string> right_lines(left_lines.size(), std::string());
            // Render a macro's full `#define NAME(params) body` to a string.
            // `max_def` truncates (with "...") when set; pass 0 for no limit
            // (used by the closer, which wants the whole definition).
            auto render_define_str = [&](std::string const& name, std::size_t max_def) -> std::string {
                if (name.empty()) return std::string();
                bool has_params = false, is_predefined = false;
                typename ContextT::position_type pos;
                std::vector<typename ContextT::token_type> parameters;
                typename ContextT::token_sequence_type definition;
                try {
                    ctx.get_macro_definition(name, has_params, is_predefined, pos, parameters, definition);
                } catch (...) {
                    return "#define " + name + "  (lookup failed)";
                }
                std::ostringstream dl;
                dl << "#define " << name;
                if (has_params) {
                    dl << "(";
                    for (std::size_t i = 0; i < parameters.size(); ++i) {
                        if (i) dl << ", ";
                        dl << parameters[i].get_value().c_str();
                    }
                    dl << ")";
                }
                dl << " ";
                for (auto const& tk : definition) dl << tk.get_value().c_str();
                std::string def = dl.str();
                if (max_def && def.size() > max_def) def = def.substr(0, max_def - 3) + "...";
                return def;
            };
            auto render_define = [&](std::size_t row, std::string const& name) {
                right_lines[row] = render_define_str(name, 48);
            };
            for (std::size_t i = 0; i < fp.frame_rows.size(); ++i) {
                render_define(fp.frame_rows[i], fp.frame_names[i]);
            }
            for (std::size_t i = 0; i < fp.rescan_frame_rows.size(); ++i) {
                render_define(fp.rescan_frame_rows[i], fp.rescan_frame_names[i]);
            }

            // -- Frame-aware diff: highlight only genuinely-new frames ----
            // Frames are rendered innermost-first (#0 = innermost). Compare
            // the outermost-first name sequences of the current and previous
            // stacks: a frame present in both (by name, from the outer end)
            // is unchanged even if its `#N` index shifted when a new frame
            // was pushed on top. Only the suffix of new names (the innermost
            // frames that have no counterpart in the previous stack) is
            // highlighted. No "removed" lines are appended — the current
            // state is rendered alone, so the old and new stacks never
            // appear together in one block.
            std::vector<std::string> cur_outer;
            for (auto it = state->expanding.begin(); it != state->expanding.end(); ++it) {
                if (it->call.empty()) continue;
                cur_outer.push_back(std::string(it->call.begin()->get_value().c_str()));
            }
            std::size_t common = 0;
            for (std::size_t i = 0;
                 i < std::min(cur_outer.size(), prev_frame_names.size()); ++i) {
                if (cur_outer[i] == prev_frame_names[i]) ++common;
                else break;
            }
            std::size_t new_count = cur_outer.size() > common
                                        ? cur_outer.size() - common : 0;
            std::vector<char> is_new_frame(left_lines.size(), 0);
            for (std::size_t i = 0; i < new_count && i < fp.frame_rows.size(); ++i) {
                is_new_frame[fp.frame_rows[i]] = 1;
            }
            prev_frame_names = cur_outer;   // snapshot for next diff

            // -- Render two-pane layout, line by line ----------------------
            constexpr int SPLIT = 50;
            bool color = color_enabled();
            frames_log_file << "── [stop " << ts << "] ──\n";
            for (std::size_t i = 0; i < left_lines.size(); ++i) {
                std::string l = left_lines[i];
                std::string r = i < right_lines.size() ? right_lines[i] : std::string();
                if (l.size() > (std::size_t)SPLIT) l = l.substr(0, SPLIT - 3) + "...";
                else if (l.size() < (std::size_t)SPLIT) l.append(SPLIT - l.size(), ' ');

                // Color wrap goes AROUND the padded cell so byte-padding
                // still puts `|` at column 50. ANSI bytes don't take columns
                // in a terminal — only the cell content does.
                if (is_new_frame[i] && color) {
                    frames_log_file << "\e[32m" << l << "\e[0m";  // green = new frame
                } else {
                    frames_log_file << l;
                }
                frames_log_file << " | " << r << "\n";
            }

            // -- Contextual closer: top-level working macro + final result ----
            // The closing separator carries the OUTERMOST live macro — the
            // top of the current expansion tree — so every stop's closer
            // names what the whole rescan/call activity is ultimately in
            // service of. The outermost frame is the first-pushed one:
            //   expanding.front()  if a substitution is in flight, else
            //   rescanning.front() if only rescans remain, else
            //   (both empty)        → a macro just completed: name its cause.
            // When a `rescanned` emptied both stacks, append the final
            // result too, so `tail -f` sees what each source-level call
            // produced at the moment it lands. The closer pads to SPLIT so
            // its `|` lines up with the two-pane rows above it; the result
            // (if any) spills into the right pane slot.
            // When a macro just completed (last_completed_result set), name
            // THAT macro — even if a parent expansion is still in flight —
            // because the result line is the headline of this stop. Otherwise
            // name the OUTERMOST live frame across both stacks: the top-level
            // macro whose source-level call started the whole expansion tree.
            // Once a macro fires `expanded` it moves to the rescan queue, and
            // all nested calls/expansions happen DURING that rescan — so the
            // oldest rescan frame (rescanning.front(), the first-pushed) is
            // the top-level macro whenever rescanning is non-empty; only when
            // the rescan queue is empty does an expanding frame own the top.
            // Render a token container (a macro call, name + args) to a
            // compact string, e.g. `BOOL(123)` — joining non-whitespace
            // tokens with a single space (but no space after `(`).
            auto render_call = [](auto const& cont) -> std::string {
                std::string s;
                for (auto const& tk : cont) {
                    if (!tk.is_valid()) continue;
                    if (IS_CATEGORY(tk, boost::wave::WhiteSpaceTokenType)) continue;
                    if (boost::wave::token_id(tk) == boost::wave::T_PLACEMARKER) continue;
                    auto v = tk.get_value().c_str();
                    if (!s.empty() && s.back() != '(' && v[0] != ')' && v[0] != '(') s += ' ';
                    s += v;
                }
                return s;
            };
            std::string top_macro;   // bare name, for the `#define` lookup
            std::string top_call;     // full call with args, e.g. `BOOL(123)`
            if (!last_completed_result.empty()) {
                top_macro = last_completed_cause;
                top_call = last_completed_call.empty() ? last_completed_cause
                                                        : last_completed_call;
            } else if (!state->rescanning.empty()) {
                auto const& f = state->rescanning.front();
                if (!f.first.empty()) {
                    top_macro = f.first.begin()->get_value().c_str();
                    top_call = render_call(f.first);
                }
            } else if (!state->expanding.empty()) {
                auto const& f = state->expanding.front();
                if (!f.call.empty()) {
                    top_macro = f.call.begin()->get_value().c_str();
                    top_call = render_call(f.call);
                }
            }
            // Left half of the closer: "── working: <call>" or, on
            // completion, "── done: <call>" — the full original source-level
            // invocation (name + args), not just the bare name. Right half:
            // the macro's FULL `#define` (no truncation). Highlighted (bright
            // magenta = working, bright yellow = done) so it stands out from
            // the per-frame rows above as the headline of the stop.
            std::string closer_left;
            bool completed = !last_completed_result.empty();
            if (completed) {
                closer_left = "── done: " + (top_call.empty() ? top_macro : top_call);
            } else {
                closer_left = "── working: ";
                if (top_call.empty()) closer_left += "(none)";
                else                  closer_left += top_call;
            }
            std::string closer_right = render_define_str(top_macro, 0);
            if ((int)closer_left.size() > SPLIT) closer_left = closer_left.substr(0, SPLIT - 3) + "...";
            else if ((int)closer_left.size() < SPLIT) closer_left.append(SPLIT - closer_left.size(), ' ');
            if (color) {
                frames_log_file << (completed ? ansi::bright_yellow_fg
                                               : ansi::bright_magenta_fg)
                                << ansi::bold;
            }
            frames_log_file << closer_left << " | " << closer_right;
            if (color) frames_log_file << ansi::reset;
            frames_log_file << "\n";

            // On completion, a dedicated RESULT banner sits below the closer
            // line so the final value is the visual headline of the stop —
            // not buried at the end of the `done:` line. Bright green +
            // bold + a `>> result >>` marker set it apart from every other
            // row in the log. Padded to SPLIT so its `|` lines up too.
            if (completed) {
                if (color) frames_log_file << "\e[92m\e[1m";  // bright green + bold
                frames_log_file << "  (result) " << last_completed_result;
                if (color) frames_log_file << ansi::reset;
                frames_log_file << "\n";
                last_completed_result.clear();  // one-shot
                last_completed_cause.clear();
                last_completed_call.clear();
            }
            frames_log_file << "──────────────────\n";
            frames_log_file.flush();
        }

        void set_frames_log_path(std::string const& path) {
            frames_log_path = path;
        }

        std::string const& get_frames_log_path() const {
            return frames_log_path;
        }

        // Banner printed ABOVE the prompt line for `calling` events. Shows the
        // macro's `#define` so the user sees what X is about to substitute
        // into, without polluting the `pp (...)` line itself.
        template <class ContextT>
        std::string make_calling_banner(std::string const& name, ContextT& ctx) const {
            bool has_params = false, is_predefined = false;
            typename ContextT::position_type pos;
            std::vector<typename ContextT::token_type> parameters;
            typename ContextT::token_sequence_type definition;
            try {
                ctx.get_macro_definition(name, has_params, is_predefined, pos, parameters, definition);
            } catch (...) {
                return "";  // no banner
            }

            std::ostringstream os;
            os << "  ┌── #define " << name;
            if (has_params) {
                os << '(';
                for (std::size_t i = 0; i < parameters.size(); ++i) {
                    if (i) os << ", ";
                    os << parameters[i].get_value().c_str();
                }
                os << ')';
            }
            os << " ──\n";
            if (has_params) {
                os << "  │   params : ";
                for (std::size_t i = 0; i < parameters.size(); ++i) {
                    if (i) os << ", ";
                    os << parameters[i].get_value().c_str();
                }
                os << "\n";
            }
            os << "  │   body   : ";
            std::string body;
            for (auto const& t : definition) body += t.get_value().c_str();
            constexpr std::size_t max_body = 60;
            if (body.size() > max_body) body = body.substr(0, max_body - 3) + "...";
            os << body;
            if (is_predefined) os << "   (predefined)";
            os << "\n  └──────────────────────────────────────────────────────────\n";
            return os.str();
        }

    private:
        template <class ContextT>
        void handle_prompt(ContextT& ctx, TokenT const& token, preprocessing_event_type type) {
            bool do_prompt = false;

            switch (mode) {
                case stepping_mode::FREE: {
                    do_prompt = true;
                    break;
                }
                case stepping_mode::UNTIL_BREAK: {
                    switch (type) {
                        case preprocessing_event_type::CALL: {
                            if (expansion_breakpoints.find(token.get_value()) != expansion_breakpoints.end())
                                do_prompt = true;
                            break;
                        }
                        case preprocessing_event_type::EXPANDED: {
                            if (expanded_breakpoints.find(token.get_value()) != expanded_breakpoints.end()) {
                                do_prompt = true;
                            }
                            break;
                        }
                        case preprocessing_event_type::RESCANNED: {
                            // `finish`: stop at the RESCANNED that drains both
                            // stacks — the completion of the whole top-level
                            // working macro tree (same condition `on_rescanned`
                            // at line 541 uses to stash the final result). The
                            // server pops the rescan frame AFTER firing the hook
                            // (server.hpp:146 then :153), so the about-to-pop
                            // frame is still counted: completion is exactly
                            // `expanding.empty() && rescanning.size() == 1`.
                            if (finish_pending && state->expanding.empty()
                                && state->rescanning.size() == 1) {
                                do_prompt = true;
                            } else if (rescanned_breakpoints.find(token.get_value()) != rescanned_breakpoints.end()) {
                                do_prompt = true;
                            }
                            break;
                        }
                        case preprocessing_event_type::LEXED: {
                            if (lexed_breakpoints.find(token.get_value()) != lexed_breakpoints.end())
                                do_prompt = true;
                            break;
                        }
                        default: break;
                    }
                    break;
                }
                case stepping_mode::UNTIL_MACRO: {
                    // `next`: skip LEXED events, stop at the next macro
                    // event (call/expand/rescan). The `steps_requested`
                    // decrement in `prompt()` still fires — it counts macro
                    // events only because LEXED never reaches `do_prompt`.
                    if (type != preprocessing_event_type::LEXED)
                        do_prompt = true;
                    break;
                }
            }

            if (do_prompt) {
                if (type == preprocessing_event_type::RESCANNED && finish_pending
                    && state->expanding.empty() && state->rescanning.size() == 1) {
                    finish_pending = false;
                }
                cli.prompt(ctx, make_trigger(type, token));
            }
        }

        server_state<ContainerT>* state;
        client_cli<TokenT, ContainerT> cli;
        std::set<typename TokenT::string_type> expansion_breakpoints;
        bool verbose = false;
        std::set<typename TokenT::string_type> expanded_breakpoints;
        std::set<typename TokenT::string_type> rescanned_breakpoints;
        std::set<typename TokenT::string_type> lexed_breakpoints;
        bool finish_pending = false;
        std::string frames_log_path;  // empty → use default_frames_log_path()
        mutable std::ofstream frames_log_file;
        mutable bool frames_log_open = false;
        // Last call-stack frame-name snapshot (outermost-first), for
        // frame-aware diff-highlighting of genuinely-new frames on the next
        // write. Stays in sync with the most-recently-written block.
        mutable std::vector<std::string> prev_frame_names;
        // The result of the most-recently-completed top-level macro
        // expansion (set in on_rescanned when both stacks empty, read once
        // then cleared by write_frames_log). Empty when the last event did
        // NOT complete a top-level macro — so the `=> ... => ...` line only
        // appears at the stop where a whole macro expansion tree finished.
        mutable std::string last_completed_cause;
        mutable std::string last_completed_call;
        mutable std::string last_completed_result;
        std::vector<numbered_bp> numbered_breakpoints;
        int next_bp_id = 1;
        stepping_mode mode;

        std::list<offset_container<ContainerT>> token_stack;
        std::vector<historical_event<ContainerT>> token_history;
        std::vector<TokenT> lexed_tokens;
        std::vector<TokenT> lex_buffer;
    };
}

#endif // PPSTEP_CLIENT_HPP