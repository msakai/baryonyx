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

#include <baryonyx/core-compare>
#include <baryonyx/core>

#include <numeric>
#include <set>
#include <stack>
#include <tuple>

#include "debug.hpp"
#include "memory.hpp"
#include "private.hpp"
#include "problem.hpp"
#include "utils.hpp"

namespace {

namespace bx = baryonyx;

struct pp_variable_access
{
    std::vector<int> in_equal_constraints;
    std::vector<int> in_greater_constraints;
    std::vector<int> in_less_constraints;
    int id;
};

class pp_lifo
{
private:
    std::vector<std::tuple<int, bool>> data;

public:
    pp_lifo(int variable, bool value)
      : data({ std::make_tuple(variable, value) })
    {}

    bool emplace(int variable, bool value)
    {
        auto it = std::find_if(
          data.cbegin(), data.cend(), [&variable](const auto& elem) {
              return std::get<0>(elem) == variable;
          });

        if (it != data.cend())
            return false;

        data.emplace_back(variable, value);

        return true;
    }

    bool push(std::tuple<int, bool> element)
    {
        auto it = std::find_if(
          data.cbegin(), data.cend(), [&element](const auto& elem) {
              return std::get<0>(elem) == std::get<0>(element);
          });

        if (it != data.cend())
            return false;

        data.push_back(element);
        return true;
    }

    bool empty() const
    {
        return data.empty();
    }

    std::tuple<int, bool> pop()
    {
        bx_ensures(!empty());

        auto ret = data.back();
        data.pop_back();
        return ret;
    }
};

class preprocessor
{
private:
    const baryonyx::context_ptr& ctx;
    const bx::problem& pb;
    std::unordered_map<int, bool> vars;
    std::vector<int> equal_constraints;
    std::vector<int> greater_constraints;
    std::vector<int> less_constraints;
    std::vector<pp_variable_access> cache;

    auto reduce(const bx::constraint& constraint) -> std::tuple<int, int, int>
    {
        int constraint_result{ constraint.value };
        int remaining_index{ -1 };

        for (int i = 0, e = bx::length(constraint.elements); i != e; ++i) {

            // Searches if the variable is already affected and then use the
            // value to update the value of the constaint.

            auto it = vars.find(constraint.elements[i].variable_index);
            if (it == vars.end()) {
                bx_ensures(remaining_index == -1);
                remaining_index = i;
            } else {
                constraint_result +=
                  -1 * (constraint.elements[i].factor * it->second);
            }
        }

        bx_ensures(remaining_index >= 0);

        return std::make_tuple(
          constraint.elements[remaining_index].factor,
          constraint.elements[remaining_index].variable_index,
          constraint_result);
    }

    auto reduce_equal_constraint(const bx::constraint& constraint)
      -> std::tuple<int, bool>
    {
        int factor, variable, result;

        std::tie(factor, variable, result) = reduce(constraint);

        bx_ensures(pb.vars.values[variable].type == bx::variable_type::binary);

        bool affect_0 = (factor * 0 == result);
        bool affect_1 = (factor * 1 == result);

        if (affect_0 && affect_1)
            return std::make_tuple(-1, false);

        if (affect_0)
            return std::make_tuple(variable, false);

        if (affect_1)
            return std::make_tuple(variable, true);

        bx_reach();
    }

    auto reduce_greater_constraint(const bx::constraint& constraint)
      -> std::tuple<int, bool>
    {
        int factor, variable, result;

        std::tie(factor, variable, result) = reduce(constraint);

        bx_ensures(pb.vars.values[variable].type == bx::variable_type::binary);

        bool affect_0 = (factor * 0 >= result);
        bool affect_1 = (factor * 1 >= result);

        if (affect_0 && affect_1)
            return std::make_tuple(-1, false);

        if (affect_0)
            return std::make_tuple(variable, false);

        if (affect_1)
            return std::make_tuple(variable, true);

        bx_reach();
    }

    auto reduce_less_constraint(const bx::constraint& constraint)
      -> std::tuple<int, bool>
    {
        int factor, variable, result;

        std::tie(factor, variable, result) = reduce(constraint);

        bx_ensures(pb.vars.values[variable].type == bx::variable_type::binary);

        bool affect_0 = (factor * 0 <= result);
        bool affect_1 = (factor * 1 <= result);

        if (affect_0 && affect_1)
            return std::make_tuple(-1, false);

        if (affect_0)
            return std::make_tuple(variable, false);

        if (affect_1)
            return std::make_tuple(variable, true);

        bx_reach();
    }

    void affect_variable(int index, bool value)
    {
        bx_expects(index >= 0 && index < bx::length(cache));

        vars[index] = value;
        pp_lifo lifo(index, value);

        while (!lifo.empty()) {
            std::tie(index, value) = lifo.pop();

            info(ctx,
                 "  - variable {} assigned to {}.\n",
                 pb.vars.names[index],
                 value);

            for (int cst : cache[index].in_equal_constraints) {
                if (equal_constraints[cst] <= 0)
                    continue;

                --equal_constraints[cst];

                if (equal_constraints[cst] == 1) {
                    info(ctx,
                         "    - equal constraint {} will be removed.\n",
                         pb.equal_constraints[cst].label);

                    auto v =
                      reduce_equal_constraint(pb.equal_constraints[cst]);
                    equal_constraints[cst] = 0;

                    if (std::get<0>(v) >= 0) {
                        vars[std::get<0>(v)] = std::get<1>(v);
                        lifo.push(v);
                    }
                }
            }

            for (int cst : cache[index].in_greater_constraints) {
                if (greater_constraints[cst] <= 0)
                    continue;

                --greater_constraints[cst];

                if (greater_constraints[cst] == 1) {
                    info(ctx,
                         "    - greater constraint {} will be removed.\n",
                         pb.greater_constraints[cst].label);

                    auto v =
                      reduce_greater_constraint(pb.greater_constraints[cst]);
                    greater_constraints[cst] = 0;

                    if (std::get<0>(v) >= 0) {
                        vars[std::get<0>(v)] = std::get<1>(v);
                        lifo.push(v);
                    }
                }
            }

            for (int cst : cache[index].in_less_constraints) {
                if (less_constraints[cst] <= 0)
                    continue;

                --less_constraints[cst];

                if (less_constraints[cst] == 1) {
                    info(ctx,
                         "    - less constraint {} will be removed.\n",
                         pb.less_constraints[cst].label);

                    auto v = reduce_less_constraint(pb.less_constraints[cst]);
                    less_constraints[cst] = 0;

                    if (std::get<0>(v) >= 0) {
                        vars[std::get<0>(v)] = std::get<1>(v);
                        lifo.push(v);
                    }
                }
            }

            vars.emplace(index, value);
        }
    }

public:
    preprocessor(const baryonyx::context_ptr& ctx_, const bx::problem& pb_)
      : ctx(ctx_)
      , pb(pb_)
      , equal_constraints(pb.equal_constraints.size())
      , greater_constraints(pb.greater_constraints.size())
      , less_constraints(pb.less_constraints.size())
      , cache(pb.vars.values.size())
    {
        // The cache stores for each variable, all constraint where element
        // variable is used.

        for (int i = 0, e = bx::length(pb.equal_constraints); i != e; ++i)
            for (const auto& elem : pb.equal_constraints[i].elements)
                cache[elem.variable_index].in_equal_constraints.emplace_back(
                  i);

        for (int i = 0, e = bx::length(pb.greater_constraints); i != e; ++i)
            for (const auto& elem : pb.greater_constraints[i].elements)
                cache[elem.variable_index].in_greater_constraints.emplace_back(
                  i);

        for (int i = 0, e = bx::length(pb.less_constraints); i != e; ++i)
            for (const auto& elem : pb.less_constraints[i].elements)
                cache[elem.variable_index].in_less_constraints.emplace_back(i);
    }

    bx::problem operator()(int variable_index, bool variable_value)
    {
        vars.clear();

        std::transform(
          pb.equal_constraints.cbegin(),
          pb.equal_constraints.cend(),
          equal_constraints.begin(),
          [](const auto& elem) { return bx::length(elem.elements); });

        std::transform(
          pb.greater_constraints.cbegin(),
          pb.greater_constraints.cend(),
          greater_constraints.begin(),
          [](const auto& elem) { return bx::length(elem.elements); });

        std::transform(
          pb.less_constraints.cbegin(),
          pb.less_constraints.cend(),
          less_constraints.begin(),
          [](const auto& elem) { return bx::length(elem.elements); });

        affect_variable(variable_index, variable_value);

        return make_problem();
    }

private:
    void constraints_exclude_copy(
      const std::vector<int>& constraints_size,
      const std::vector<bx::constraint>& constraints,
      std::vector<bx::constraint>& copy) const
    {
        size_t i, e;

        for (i = 0, e = constraints.size(); i != e; ++i) {

            // Remaining constraints with one element are undecidable (can be 0
            // or 1) but useless in constraints (e.g. x <= 1) list. We remove
            // it.

            if (constraints_size[i] <= 1)
                continue;

            if (constraints_size[i] == bx::length(constraints[i].elements)) {
                copy.push_back(constraints[i]);
            } else {
                copy.emplace_back();
                copy.back().id = constraints[i].id;
                copy.back().label = constraints[i].label;
                copy.back().value = constraints[i].value;

                for (const auto& elem : constraints[i].elements) {
                    auto it = vars.find(elem.variable_index);

                    if (it == vars.end()) {
                        copy.back().elements.push_back(elem);
                    } else {
                        if (it->second == true)
                            copy.back().value -= elem.factor;
                    }
                }
            }
        }
    }

    auto make_problem() const -> bx::problem
    {
        bx::problem copy;

        copy.type = pb.type;
        copy.problem_type = pb.problem_type;

        copy.objective = objective_function_exclude_copy();
        std::tie(copy.vars, copy.affected_vars) = variables_exclude_copy();

        constraints_exclude_copy(
          equal_constraints, pb.equal_constraints, copy.equal_constraints);
        constraints_exclude_copy(greater_constraints,
                                 pb.greater_constraints,
                                 copy.greater_constraints);
        constraints_exclude_copy(
          less_constraints, pb.less_constraints, copy.less_constraints);

        return copy;
    }

    auto objective_function_exclude_copy() const -> bx::objective_function
    {
        bx::objective_function ret;

        ret.elements.reserve(pb.objective.elements.size() - vars.size());
        ret.value = pb.objective.value;

        for (const auto& elem : pb.objective.elements) {
            auto it = vars.find(elem.variable_index);

            if (it == vars.cend()) {
                ret.elements.emplace_back(elem.factor, elem.variable_index);
            } else {
                ret.value += elem.factor * (it->second ? 1 : 0);
            }
        }

        return ret;
    }

    auto variables_exclude_copy() const
      -> std::tuple<bx::variables, bx::affected_variables>
    {
        bx::variables ret_vars;
        bx::affected_variables ret_aff_vars;

        ret_vars.names.reserve(pb.vars.names.size() - vars.size());
        ret_vars.values.reserve(pb.vars.names.size() - vars.size());
        ret_aff_vars.names.reserve(vars.size() +
                                   pb.affected_vars.names.size());
        ret_aff_vars.values.reserve(vars.size() +
                                    pb.affected_vars.names.size());

        ret_aff_vars.names = pb.affected_vars.names;
        ret_aff_vars.values = pb.affected_vars.values;

        for (std::size_t i = 0, e = pb.vars.values.size(); i != e; ++i) {
            auto it = vars.find(static_cast<int>(i));

            if (it == vars.cend()) {
                ret_vars.names.emplace_back(pb.vars.names[i]);
                ret_vars.values.emplace_back(pb.vars.values[i]);
            } else {
                ret_aff_vars.names.emplace_back(pb.vars.names[i]);
                ret_aff_vars.values.emplace_back(it->second);
            }
        }

        return std::make_tuple(ret_vars, ret_aff_vars);
    }
};

} // namespace anonymous

namespace baryonyx {

std::tuple<problem, problem>
split(const context_ptr& ctx, const problem& pb, int variable_index_to_affect)
{
    info(ctx,
         "- Preprocessor starts split of variable {} (size: {})\n",
         pb.vars.names[variable_index_to_affect],
         to_string(memory_consumed_size(memory_consumed(pb))));

    ::preprocessor pp(ctx, pb);

    return std::make_tuple(pp(variable_index_to_affect, true),
                           pp(variable_index_to_affect, false));
}

problem
affect(const context_ptr& ctx,
       const problem& pb,
       int variable_index,
       bool variable_value)
{
    info(ctx,
         "- Preprocessor starts affectation of variable {} to {} (size: {})\n",
         pb.vars.names[variable_index],
         variable_value,
         to_string(memory_consumed_size(memory_consumed(pb))));

    ::preprocessor pp(ctx, pb);

    return pp(variable_index, variable_value);
}

} // namespace baryonyx
