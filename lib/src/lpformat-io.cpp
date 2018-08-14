/* Copyright (C) 2016-2018 INRA
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <baryonyx/core>

#include <fmt/format.h>

#include "utils.hpp"

#include <deque>
#include <istream>
#include <limits>
#include <ostream>
#include <unordered_map>

using namespace baryonyx;

static inline bool
iequals(const std::string& lhs, const std::string& rhs) noexcept
{
    auto sz = lhs.size();

    if (rhs.size() != sz)
        return false;

    for (std::string::size_type i = 0; i < sz; ++i)
        if (std::tolower(lhs[i]) != std::tolower(rhs[i]))
            return false;

    return true;
}

static inline bool
is_operator(int c) noexcept
{
    return c == '<' || c == '>' || c == '=';
}

static inline bool
is_valid_character(int c) noexcept
{
    if (std::isalnum(c))
        return true;

    switch (c) {
    case '!':
    case '"':
    case '#':
    case '$':
    case '%':
    case '&':
    case '(':
    case ')':
    case ',':
    case '.':
    case ';':
    case '?':
    case '@':
    case '_':
    case '{':
    case '}':
    case '~':
        return true;
    default:
        return false;
    }
}

struct parser_stack
{
    parser_stack(std::istream& is_)
      : m_is(is_)
      , m_line(0)
      , m_column(0)
      , m_current_constraint_id(0)
      , m_coefficient_type(0)
      , m_eof_reached(is_.eof())
    {}

    int peek()
    {
        if (stack.empty())
            fill();

        if (stack.empty())
            return EOF;

        return stack[0][0];
    }

    std::string top()
    {
        if (stack.empty())
            fill();

        if (stack.empty())
            throw file_format_failure(file_format_error_tag::end_of_file,
                                      static_cast<int>(m_line),
                                      static_cast<int>(m_column));

        return stack.front();
    }

    std::string pop()
    {
        if (stack.empty())
            fill();

        if (stack.empty())
            throw file_format_failure(file_format_error_tag::end_of_file,
                                      static_cast<int>(m_line),
                                      static_cast<int>(m_column));

        std::string ret = stack.front();
        std::tie(m_line, m_column) = m_position_stack.front();
        stack.pop_front();
        m_position_stack.pop_front();

        return ret;
    }

    bool is_topic()
    {
        auto str = top();

        if (stack.empty())
            return false;

        if (iequals(str, "binary") || iequals(str, "binaries") ||
            iequals(str, "bound") || iequals(str, "bounds") ||
            iequals(str, "general") || iequals(str, "end") ||
            iequals(str, "st") || iequals(str, "st:"))
            return true;

        if (stack.size() > 2 &&
            (iequals(str, "subject") && iequals(stack[1], "to") &&
             iequals(stack[2], ":")))
            return true;

        if (stack.size() > 1 &&
            ((iequals(str, "subject") && iequals(stack[1], "to")) ||
             (iequals(str, "subject") && iequals(stack[1], "to:")) ||
             (iequals(str, "st") && iequals(stack[1], ":"))))
            return true;

        return false;
    }

    /**
     * @brief Tries to read on of the constraint title syntax.
     *
     * @details This function tries to read the constraint title that can be
     *      any of 'st', 'st:', 'subject to', 'subject to:' or 'subject to :'.
     */
    inline bool is_subject_to()
    {
        if (stack.empty())
            return false;

        if (iequals(stack[0], "st") || iequals(stack[0], "st:")) {
            pop();
            return true;
        }

        if (stack.size() >= 2 &&
            (iequals(stack[0], "subject") && iequals(stack[1], "to") &&
             iequals(stack[2], ":"))) {
            pop();
            pop();
            pop();
            return true;
        }

        if (stack.size() > 1 &&
            ((iequals(stack[0], "st") && iequals(stack[1], ":")) ||
             (iequals(stack[0], "subject") && iequals(stack[1], "to")) ||
             (iequals(stack[0], "subject") && iequals(stack[1], "to:")))) {
            pop();
            pop();
            return true;
        }

        return false;
    }

    inline bool is_bounds()
    {
        if (stack.empty())
            fill();

        if (stack.size() <= 0)
            return false;

        if (iequals(stack[0], "bounds") || iequals(stack[0], "bound")) {
            pop();
            return true;
        }

        return false;
    }

    inline bool is_binary()
    {
        if (stack.empty())
            fill();

        if (stack.size() <= 0)
            return false;

        if (iequals(stack[0], "binary") || iequals(stack[0], "binaries")) {
            pop();
            return true;
        }

        return false;
    }

    inline bool is_general()
    {
        if (stack.empty())
            fill();

        if (stack.size() <= 0)
            return false;

        if (iequals(stack[0], "general")) {
            pop();
            return true;
        }

        return false;
    }

    inline bool is_end()
    {
        if (stack.empty())
            fill();

        if (stack.size() <= 0)
            return false;

        if (iequals(stack[0], "end")) {
            pop();
            return true;
        }

        return false;
    }

    inline bool is_integer()
    {
        if (stack.empty())
            fill();

        if (stack.size() <= 0)
            return false;

        if (std::isdigit(stack[0][0]))
            return true;

        //
        // If the first character is '-', we need to ensure the next character
        // is a digit to avoid the "-x1" string.
        //

        if (stack[0][0] == '-') {
            if (stack[0].size() > 1)
                return !!std::isdigit(stack[0][1]);

            if (stack.size() > 2)
                return !!std::isdigit(stack[1][0]);
        }

        return false;
    }

    void push_front(std::string str)
    {
        auto length = static_cast<int>(str.length());
        m_position_stack.emplace_front(m_line, m_column + (m_column - length));

        stack.push_front(std::move(str));
    }

    void substr_front(std::string::size_type i)
    {
        if (stack[0].size() > i) {
            m_position_stack.emplace_front(m_line, m_column - i);
            stack[0] = stack[0].substr(i, std::string::npos);
        } else {
            m_position_stack.pop_front();
            stack.pop_front();
        }
    }

    std::deque<std::string> stack;

    bool empty() const
    {
        return stack.empty();
    }

    std::string::size_type line() const
    {
        return m_line;
    }

    std::string::size_type column() const
    {
        return m_column;
    }

    std::unordered_map<std::string, int>& cache()
    {
        return m_variable_cache;
    }

    int current_constraint_id() const noexcept
    {
        return m_current_constraint_id;
    }

    void increase_current_constaint_id() noexcept
    {
        ++m_current_constraint_id;
    }

    void update_coefficient(int factor) noexcept
    {
        if (m_coefficient_type == 2)
            return;

        if (factor < -1 || factor > 1) {
            m_coefficient_type = 2;
            return;
        }

        if (factor == -1) {
            m_coefficient_type = 1;
        }
    }

    int coefficient() const noexcept
    {
        return m_coefficient_type;
    }

private:
    std::deque<std::tuple<std::string::size_type, std::string::size_type>>
      m_position_stack;
    std::unordered_map<std::string, int> m_variable_cache;

    std::istream& m_is;
    std::string::size_type m_line;
    std::string::size_type m_column;
    int m_current_constraint_id;
    int m_coefficient_type; // 0 {0, 1}, 1 {-1 0 1}, 2 {Z}
    bool m_eof_reached;

    void fill()
    {
        std::string line;
        int cache{ 256 };

        while (m_is.good()) {
            std::getline(m_is, line);
            m_line++;
            cache--;

            if (line.empty() && m_is.eof())
                return;

            std::string::size_type i{ 0 };
            std::string::size_type e{ line.size() };

            for (; i != e; ++i)
                if (!std::isspace(line[i]))
                    break;

            if (i != e && line[i] == '\\')
                continue;

            while (i != e) {
                m_position_stack.emplace_back(m_line, i);
                stack.emplace_back();

                for (; i != e; ++i) {
                    if (std::isspace(line[i]))
                        break;
                    else
                        stack.back() += line[i];
                }

                for (; i != e; ++i)
                    if (!std::isspace(line[i]))
                        break;
            }

            if (cache <= 0 && !stack.empty())
                return;

            if (stack.empty())
                cache = 256;
        }
    }
};

static inline int
get_variable(std::unordered_map<std::string, int>& cache,
             baryonyx::variables& vars,
             const std::string& name)
{
    auto it = cache.find(name);
    if (it != cache.end())
        return it->second;

    auto id = std::distance(vars.names.cbegin(), vars.names.cend());
    if (id >= INT_MAX)
        throw file_format_failure(file_format_error_tag::too_many_variables);

    vars.names.emplace_back(name);
    vars.values.emplace_back();

    cache[name] = static_cast<int>(id);

    return static_cast<int>(id);
}

static inline int
get_variable_only(std::unordered_map<std::string, int>& cache,
                  const std::string& name) noexcept
{
    auto it = cache.find(name);
    if (it != cache.end())
        return it->second;

    return -1;
}

static inline std::string
read_name(parser_stack& stack)
{
    std::string str = stack.top();
    std::string::size_type i = { 0 }, e = { str.length() };
    std::string ret;

    if (std::isalpha(str[i]) || str[i] == '_') {
        ret += str[i];
        ++i;

        for (; i != e; ++i) {
            if (is_valid_character(str[i]))
                ret += str[i];
            else
                break;
        }

        stack.substr_front(i);

        return ret;
    }

    throw file_format_failure(file_format_error_tag::bad_name,
                              static_cast<int>(stack.line()),
                              static_cast<int>(stack.column()));
}

static inline operator_type
read_operator(parser_stack& stack)
{
    std::string str = stack.top();

    if (str[0] == '<') {
        if (str.size() > 1 && str[1] == '=') {
            stack.substr_front(2);
        } else {
            stack.substr_front(1);
        }

        return operator_type::less;
    }

    if (str[0] == '>') {
        if (str.size() > 1 && str[1] == '=') {
            stack.substr_front(2);
        } else {
            stack.substr_front(1);
        }

        return operator_type::greater;
    }

    if (str[0] == '=') {
        if (str.size() > 1 && str[1] == '<') {
            stack.substr_front(2);

            return operator_type::less;
        } else if (str.size() > 1 && str[1] == '=') {
            stack.substr_front(2);

            return operator_type::greater;
        }

        stack.substr_front(1);
        return operator_type::equal;
    }

    throw file_format_failure(file_format_error_tag::bad_operator,
                              static_cast<int>(stack.line()),
                              static_cast<int>(stack.column()));
}

static inline int
read_integer(parser_stack& stack)
{
    std::string str = stack.pop();
    bool negative = false;

    if (str[0] == '-') {
        negative = true;

        if (str.size() > 1) {
            str = str.substr(1);
        } else {
            str = stack.pop();
        }
    }

    char* endptr;
    errno = 0;
    long value = std::strtol(str.c_str(), &endptr, 10);

    if ((errno == ERANGE && (value == LONG_MIN || value == LONG_MAX)) ||
        (value == 0 && endptr == str.c_str()))
        throw file_format_failure(file_format_error_tag::bad_integer,
                                  static_cast<int>(stack.line()),
                                  static_cast<int>(stack.column()));

    if (value < INT_MIN)
        throw file_format_failure(file_format_error_tag::bad_integer,
                                  static_cast<int>(stack.line()),
                                  static_cast<int>(stack.column()));

    if (value > INT_MAX)
        throw file_format_failure(file_format_error_tag::bad_integer,
                                  static_cast<int>(stack.line()),
                                  static_cast<int>(stack.column()));

    if (*endptr != '\0')
        stack.push_front(endptr);

    return (negative) ? -static_cast<int>(value) : static_cast<int>(value);
}

static inline double
read_double(parser_stack& stack)
{
    std::string str = stack.pop();

    bool negative = false;

    if (str[0] == '-') {
        negative = true;

        if (str.size() > 1) {
            str = str.substr(1);
        } else {
            str = stack.pop();
        }
    }

    char* endptr;
    errno = 0;
    double value = std::strtod(str.c_str(), &endptr);
    if ((errno == ERANGE && (value == HUGE_VAL || value == -HUGE_VAL)) ||
        (value == 0.0 && endptr == str.c_str()))
        throw file_format_failure(file_format_error_tag::bad_integer,
                                  static_cast<int>(stack.line()),
                                  static_cast<int>(stack.column()));

    if (*endptr != '\0')
        stack.push_front(endptr);

    return (negative) ? -value : value;
}

static inline std::tuple<std::string, int>
read_function_element(parser_stack& stack)
{
    bool negative{ false };
    std::tuple<std::string, int> ret{ std::string(), { 1 } };

    std::string str = stack.pop();

    if (str[0] == '-' || str[0] == '+') {
        negative = (str[0] == '-');

        if (str.length() != 1)
            stack.push_front(str.substr(1, std::string::npos));
    } else {
        stack.push_front(str);
    }

    str = stack.top();

    {
        if (std::isdigit(str[0])) {
            std::get<1>(ret) = read_integer(stack);
            if (negative)
                std::get<1>(ret) *= -1;
        } else if (negative) {
            std::get<1>(ret) = -1;
        }

        stack.update_coefficient(std::get<1>(ret));
    }

    if (stack.is_topic())
        return ret;

    str = stack.top();

    if (std::isalpha(str[0]) || str[0] == '_')
        std::get<0>(ret) = read_name(stack);

    return ret;
}

static inline std::tuple<std::string, double>
read_objective_function_element(parser_stack& stack)
{
    bool negative{ false };
    std::tuple<std::string, double> ret{ std::string(), { 1.0 } };

    std::string str = stack.pop();

    if (str[0] == '-' || str[0] == '+') {
        negative = (str[0] == '-');

        if (str.length() != 1)
            stack.push_front(str.substr(1, std::string::npos));
    } else {
        stack.push_front(str);
    }

    str = stack.top();

    {
        if (std::isdigit(str[0])) {
            std::get<1>(ret) = read_double(stack);
            if (negative)
                std::get<1>(ret) *= -1.0;
        } else if (negative) {
            std::get<1>(ret) = -1.0;
        }
    }

    if (stack.is_topic())
        return ret;

    str = stack.top();

    if (std::isalpha(str[0]) || str[0] == '_')
        std::get<0>(ret) = read_name(stack);

    return ret;
}

static inline objective_function_type
read_objective_function_type(parser_stack& stack)
{
    auto str = stack.top();

    std::string ret;
    std::string::size_type i{ 0 }, e{ str.length() };

    if (std::isalpha(str[i])) {
        for (; i != e; ++i) {
            if (std::isalpha(str[i]))
                ret += str[i];
            else
                break;
        }

        stack.substr_front(i);
    }

    if (iequals(ret, "maximize"))
        return objective_function_type::maximize;

    if (iequals(ret, "minimize"))
        return objective_function_type::minimize;

    throw file_format_failure(
      file_format_error_tag::bad_objective_function_type,
      static_cast<int>(stack.line()),
      static_cast<int>(stack.column()));
}

static inline objective_function
read_objective_function(parser_stack& stack, raw_problem& p)
{
    objective_function ret;

    if (stack.is_topic())
        return ret;

    //
    // Forget the `obj:' string append by cplex.
    //

    if (std::isalpha(stack.peek()) || stack.peek() == '_') {
        auto tmp = read_name(stack);

        if (stack.peek() == ':') {
            stack.substr_front(1);
        } else {
            stack.push_front(tmp);
        }
    }

    while (!stack.is_topic()) {
        auto elem = read_objective_function_element(stack);

        if (std::get<0>(elem).empty()) // we read a constant
            ret.value += std::get<1>(elem);
        else
            ret.elements.emplace_back(
              std::get<1>(elem),
              get_variable(stack.cache(), p.vars, std::get<0>(elem)));
    }

    return ret;
}

static inline std::tuple<constraint, operator_type>
read_constraint(parser_stack& stack, raw_problem& p)
{
    constraint cst;
    std::string label;

    if (std::isalpha(stack.peek()) || stack.peek() == '_') {
        auto tmp = read_name(stack);

        if (stack.peek() == ':') {
            label = tmp;
            stack.substr_front(1);
        } else {
            cst.elements.emplace_back(
              1, get_variable(stack.cache(), p.vars, tmp));
        }
    }

    auto str = stack.top();

    if (!iequals(str, "bound") && !iequals(str, "bounds") &&
        !iequals(str, "binary") && !iequals(str, "binaries") &&
        !iequals(str, "general") && !iequals(str, "end")) {

        while (!is_operator(stack.peek()) && !iequals(str, "binary") &&
               !iequals(str, "bound") && !iequals(str, "bounds") &&
               !iequals(str, "binaries") && !iequals(str, "general") &&
               !iequals(str, "end")) {
            auto elem = read_function_element(stack);
            cst.elements.emplace_back(
              std::get<1>(elem),
              get_variable(stack.cache(), p.vars, std::get<0>(elem)));
        }

        operator_type type = read_operator(stack);
        cst.label = label;
        cst.value = read_integer(stack);

        return std::make_tuple(cst, type);
    }

    throw file_format_failure(file_format_error_tag::bad_constraint,
                              static_cast<int>(stack.line()),
                              static_cast<int>(stack.column()));
}

static inline void
read_constraints(parser_stack& stack, raw_problem& p)
{
    auto str = stack.top();

    while (!iequals(str, "binary") && !iequals(str, "binaries") &&
           !iequals(str, "bound") && !iequals(str, "bounds") &&
           !iequals(str, "general") && !iequals(str, "end")) {

        auto cst = read_constraint(stack, p);
        std::get<0>(cst).id = stack.current_constraint_id();

        switch (std::get<1>(cst)) {
        case operator_type::equal:
            p.equal_constraints.emplace_back(std::get<0>(cst));
            break;
        case operator_type::greater:
            p.greater_constraints.emplace_back(std::get<0>(cst));
            break;
        case operator_type::less:
            p.less_constraints.emplace_back(std::get<0>(cst));
            break;
        default:
            throw file_format_failure(file_format_error_tag::unknown,
                                      static_cast<int>(stack.line()),
                                      static_cast<int>(stack.column()));
        }

        if (std::get<0>(cst).label.empty())
            std::get<0>(cst).label =
              fmt::format("ct{}", stack.current_constraint_id());

        stack.increase_current_constaint_id();
        str = stack.top();
    }
}

static inline void
apply_bound(int value, operator_type type, variable_value& variable)
{
    switch (type) {
    case operator_type::greater:
        variable.max = value;
        break;
    case operator_type::less:
        variable.min = value;
        break;
    case operator_type::equal:
        variable.min = value;
        variable.max = value;
        break;
    }
}

static inline void
apply_bound(variable_value& variable, operator_type type, int value)
{
    switch (type) {
    case operator_type::greater:
        variable.min = value;
        break;
    case operator_type::less:
        variable.max = value;
        break;
    case operator_type::equal:
        variable.min = value;
        variable.max = value;
        break;
    }
}

static inline void
read_bound(parser_stack& stack, raw_problem& p)
{
    /*
     * If first character is a digit, tries to read the bound:
     * value [<|<=|=|>|>=] variable_name [<|<=|=|>|>=] value or
     * value [<|<=|=|>|>=] variable_name
     */
    if (std::isdigit(stack.peek())) {
        auto value_first = read_integer(stack);
        auto operator_type_first = read_operator(stack);
        auto variable = read_name(stack);
        auto id = get_variable(stack.cache(), p.vars, variable);

        apply_bound(value_first, operator_type_first, p.vars.values[id]);

        /*
         * If next character is a <, > or =, then tries to read second part
         * of
         * the bound of: value [<|<=|=|>|>=] variable_name [<|<=|=|>|>=]
         * value
         */
        if (is_operator(stack.peek())) {
            auto operator_type_second = read_operator(stack);
            auto value_second = read_integer(stack);

            apply_bound(p.vars.values[id], operator_type_second, value_second);
        }
    } else {
        /*
         * Tries to read the bound: variable_name [>|>=|=|<|<=] value
         */
        auto variable = read_name(stack);
        auto operator_type = read_operator(stack);
        auto value = read_integer(stack);
        auto id = get_variable(stack.cache(), p.vars, variable);
        apply_bound(p.vars.values[id], operator_type, value);
    }
}

static inline void
read_bounds(parser_stack& stack, raw_problem& p)
{
    auto str = stack.top();

    while (!iequals(str, "binary") && !iequals(str, "binaries") &&
           !iequals(str, "general") && !iequals(str, "end")) {
        read_bound(stack, p);
        str = stack.top();
    }
}

static inline void
read_binary(parser_stack& stack, raw_problem& p)
{
    auto str = stack.top();

    while (!iequals(str, "general") && !iequals(str, "end")) {
        auto name = read_name(stack);
        auto id = get_variable_only(stack.cache(), name);

        if (id < 0 || p.vars.values[id].type != variable_type::real)
            throw file_format_failure(name,
                                      file_format_error_tag::unknown,
                                      static_cast<int>(stack.line()),
                                      static_cast<int>(stack.column()));

        p.vars.values[id] = { 0, 1, variable_type::binary };

        str = stack.top();
    }
}

static inline void
read_general(parser_stack& stack, raw_problem& p)
{
    auto str = stack.top();

    while (!iequals(str, "end")) {
        auto name = read_name(stack);
        auto id = get_variable_only(stack.cache(), name);

        if (id < 0 || p.vars.values[id].type != variable_type::real)
            throw file_format_failure(name,
                                      file_format_error_tag::unknown,
                                      static_cast<int>(stack.line()),
                                      static_cast<int>(stack.column()));

        p.vars.values[id].type = variable_type::general;

        str = stack.top();
    }
}

namespace baryonyx {

baryonyx::raw_problem
read_problem(std::istream& is)
{
    raw_problem p;
    parser_stack stack(is);
    std::string toek;

    p.type = read_objective_function_type(stack);
    p.objective = read_objective_function(stack, p);

    if (stack.is_subject_to())
        read_constraints(stack, p);

    if (stack.is_bounds())
        read_bounds(stack, p);

    if (stack.is_binary())
        read_binary(stack, p);

    if (stack.is_general())
        read_general(stack, p);

    if (stack.is_end()) {
        if (stack.empty()) {
            return p;
        }
    }

    throw file_format_failure("end",
                              file_format_error_tag::incomplete,
                              static_cast<int>(stack.line()),
                              static_cast<int>(stack.column()));
}
}
