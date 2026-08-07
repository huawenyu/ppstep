#ifndef PPSTEP_CLIENT_HPP
#define PPSTEP_CLIENT_HPP

#include <vector>
#include <stack>
#include <optional>
#include <variant>
#include <tuple>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <set>
#include <functional>

#include "server_fwd.hpp"
#include "client_fwd.hpp"
#include "view.hpp"
#include "utils.hpp"

namespace ppstep {
    namespace ansi {
        constexpr auto black_fg = "\u001b[30m";
        constexpr auto white_fg = "\u001b[37;1m";

        // Backgrounds chosen to not collide with token fg colors
        // (cyan / white / magenta / green / yellow).
        constexpr auto magenta_bg     = "\u001b[45;1m";  // call
        constexpr auto yellow_bg      = "\u001b[43m";    // expanded
        constexpr auto bright_blue_bg = "\u001b[44;1m";  // rescanned

        constexpr auto bold = "\u001b[1m";

        constexpr auto reset = "\u001b[0m";
    }

    namespace events {
        template <class ContainerT, class DerivedT>
        struct formatting_event {
            formatting_event(std::size_t start, std::size_t end) : start(start), end(end) {}

            void print(std::ostream& os, ContainerT const& tokens) const {
                auto sub_start = std::next(tokens.begin(), start);
                auto sub_end = std::next(tokens.begin(), end);

                auto it = tokens.begin();
                auto end = tokens.end();

                bool color = ppstep::color_enabled();

                if (color) os << ansi::bold;
                print_token_range(os, it, sub_start);
                if (it != tokens.begin())
                    os << ' ';

                if (color) static_cast<DerivedT const*>(this)->format(os);
                if (sub_start != sub_end) {
                    print_token_range(os, sub_start, sub_end);
                    if (color) os << ansi::reset;
                } else {
                    os << ' ';
                    if (color) os << ansi::reset;
                }
                if (color) os << ansi::bold;
                if (sub_end != end)
                    os << ' ';

                print_token_range(os, sub_end, tokens.end());
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
                os << ansi::magenta_bg << ansi::white_fg;
            }

            void explain(std::ostream& os) const {
                os << "called macro " << ansi::magenta_bg << ansi::white_fg;
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
                os << ansi::yellow_bg << ansi::black_fg;
            }

            void explain(std::ostream& os) const {
                os << "expanded macro " << ansi::yellow_bg << ansi::black_fg;
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
            rescanned(ContainerT cause, ContainerT initial, std::size_t start, std::size_t end)
                : formatting_event<ContainerT, rescanned<ContainerT>>(start, end), cause(std::move(cause)), initial(std::move(initial)) {}

            void format(std::ostream& os) const {
                os << ansi::bright_blue_bg << ansi::white_fg;
            }

            void explain(std::ostream& os) const {
                os << "rescanned macro " << ansi::yellow_bg << ansi::black_fg;
                print_token_container(os, initial) << ansi::reset << "\ncaused by " << ansi::magenta_bg << ansi::white_fg;
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

            ContainerT cause, initial;
        };

        template <class ContainerT>
        struct lexed {
            void print(std::ostream& os, ContainerT const& tokens) const {
                if (ppstep::color_enabled()) os << ansi::bold;
                print_token_container(os, tokens);
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
                     events::rescanned<ContainerT>(cause, initial, lexed_tokens.size() + new_start, lexed_tokens.size() + new_end));

            } catch (std::logic_error const&) {
                push(ContainerT(result), events::rescanned<ContainerT>(cause, initial, lexed_tokens.size() + 0, lexed_tokens.size() + result.size()));
            }

            // Use `cause` (the macro whose body was just rescanned) for the
            // prompt token — `initial` here is the rescan input, whose first
            // token may be a parenthesis or a literal, not the macro name.
            handle_prompt(ctx, *(cause.begin()), preprocessing_event_type::RESCANNED);
        }
        
        template <typename ContextT, typename ExceptionT>
        void on_exception(ContextT& ctx, ExceptionT const& e) {
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

        // `finish` records the current expansion depth; the next `expanded`
        // event at exactly that depth is the one that pops back to the caller.
        void arm_finish() {
            if (state->expanding.empty()) return;
            finish_pending = true;
            finish_target_depth = state->expanding.size();
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
            std::string name(token.get_value().c_str());
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
                if (it->empty()) continue;
                std::string name(it->begin()->get_value().c_str());
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
                            // `finish`: stop on the next expansion that pops back
                            // to the depth we recorded (skipping nested calls of
                            // the same macro name).
                            if (finish_pending && state->expanding.size() == finish_target_depth) {
                                do_prompt = true;
                            } else if (expanded_breakpoints.find(token.get_value()) != expanded_breakpoints.end()) {
                                do_prompt = true;
                            }
                            break;
                        }
                        case preprocessing_event_type::RESCANNED: {
                            if (rescanned_breakpoints.find(token.get_value()) != rescanned_breakpoints.end())
                                do_prompt = true;
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
            }

            if (do_prompt) {
                if (type == preprocessing_event_type::EXPANDED && finish_pending
                    && state->expanding.size() == finish_target_depth) {
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
        std::size_t finish_target_depth = 0;
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