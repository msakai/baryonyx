/* Copyright (C) 2016-2019 INRA
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

#ifndef ORG_VLEPROJECT_BARYONYX_LIB_PRIVATE_RESUME_HPP
#define ORG_VLEPROJECT_BARYONYX_LIB_PRIVATE_RESUME_HPP

#include <array>
#include <chrono>
#include <iomanip>
#include <iterator>
#include <numeric>
#include <ostream>

#include <baryonyx/core-utils>
#include <baryonyx/core>

#include "problem.hpp"

namespace baryonyx {

struct resume
{
    template<typename Problem>
    resume(const Problem& pb, bool use_lp_format_ = true)
      : variables({})
      , constraints({})
      , minmax(compute_min_max_objective_function(pb.objective))
      , use_lp_format(use_lp_format_)
    {
        variables = std::accumulate(pb.vars.values.begin(),
                                    pb.vars.values.end(),
                                    variables,
                                    [](std::array<int, 3>& value, auto vv) {
                                        switch (vv.type) {
                                        case variable_type::real:
                                            value[0]++;
                                            break;
                                        case variable_type::binary:
                                            value[1]++;
                                            break;
                                        case variable_type::general:
                                            value[2]++;
                                            break;
                                        }

                                        return value;
                                    });

        constraints[0] = static_cast<int>(pb.equal_constraints.size());
        constraints[1] = static_cast<int>(pb.greater_constraints.size());
        constraints[2] = static_cast<int>(pb.less_constraints.size());

        problem_type = get_problem_type(pb);
    }

    std::string get_problem_type(const problem& pb) const
    {
        switch (pb.problem_type) {
        case baryonyx::problem_solver_type::equalities_01:
            return "equalities-01";
        case baryonyx::problem_solver_type::equalities_101:
            return "equalities-101";
        case baryonyx::problem_solver_type::equalities_Z:
            return "equalities-Z";
        case baryonyx::problem_solver_type::inequalities_01:
            return "inequalities-01";
        case baryonyx::problem_solver_type::inequalities_101:
            return "inequalities-101";
        case baryonyx::problem_solver_type::inequalities_Z:
            return "inequalities-Z";
        default:
            return {};
        }
    }

    std::string get_problem_type(const raw_problem&) const
    {
        return {};
    }

    std::array<int, 3> variables;
    std::array<int, 3> constraints;
    std::tuple<double, double> minmax;
    std::string problem_type;
    bool use_lp_format;
};

inline std::ostream&
operator<<(std::ostream& os, const resume& pb)
{
    auto store = os.flags();

    os << std::setprecision(std::numeric_limits<double>::digits10 + 1);

    if (pb.use_lp_format) {
        os << "\\ Problem statistics:\n"
           << R"(\  type: )" << pb.problem_type << '\n'
           << R"(\  nb variables: )"
           << std::accumulate(pb.variables.begin(), pb.variables.end(), 0)
           << '\n'
           << R"(\   ..... real: )" << pb.variables[0] << '\n'
           << R"(\   ... binary: )" << pb.variables[1] << '\n'
           << R"(\   .. general: )" << pb.variables[2] << '\n'
           << R"(\  nb constraints: )"
           << std::accumulate(pb.constraints.begin(), pb.constraints.end(), 0)
           << '\n'
           << R"(\   ........ =  : )" << pb.constraints[0] << '\n'
           << R"(\   ........ >= : )" << pb.constraints[1] << '\n'
           << R"(\   ........ <= : )" << pb.constraints[2] << '\n'
           << R"(\  minimal value.: )" << std::get<0>(pb.minmax) << '\n'
           << R"(\  maximal value.: )" << std::get<1>(pb.minmax) << '\n';
    } else {
        os << "Problem statistics:\n"
           << "  * type: " << pb.problem_type << '\n'
           << "  * variables: "
           << std::accumulate(pb.variables.begin(), pb.variables.end(), 0)
           << '\n'
           << "    - real: " << pb.variables[0] << '\n'
           << "    - binary: " << pb.variables[1] << '\n'
           << "    - general: " << pb.variables[2] << '\n'
           << "  * constraints: "
           << std::accumulate(pb.constraints.begin(), pb.constraints.end(), 0)
           << '\n'
           << "    - constraint =  : " << pb.constraints[0] << '\n'
           << "    - constraint >= : " << pb.constraints[1] << '\n'
           << "    - constraint <= : " << pb.constraints[2] << '\n'
           << "  * objective:\n"
           << "    - minimal value.: " << std::get<0>(pb.minmax) << '\n'
           << "    - maximal value.: " << std::get<1>(pb.minmax) << '\n';
    }

    os.flags(store);

    return os;
}

inline std::ostream&
operator<<(std::ostream& os, const affected_variables& var)
{
    std::size_t i = 0, e = var.names.size();

    for (; i != e; ++i)
        os << var.names[i] << ": " << (var.values[i] ? 1 : 0) << '\n';

    return os;
}

} // namespace baryonyx

#endif
