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

#include "itm-common.hpp"
#include "itm-optimizer-common.hpp"
#include "itm-solver-common.hpp"

#include <algorithm>

namespace baryonyx {
namespace itm {

template<typename T>
struct fake_vector
{
    template<typename Integer>
    T operator[](Integer /*i*/) const noexcept
    {
        return T{ 0 };
    }
};

template<typename Float, typename Mode, typename Random>
struct solver_random_inequalities_101coeff
{
    using mode_type = Mode;
    using float_type = Float;

    Random& rng;

    struct rc_data
    {
        Float value;
        int id;
        int a;

        constexpr bool is_negative() const
        {
            return value < 0;
        }
    };

    sparse_matrix<int> ap;
    std::unique_ptr<int[]> A;
    std::unique_ptr<rc_data[]> R;
    std::unique_ptr<bound[]> b;
    fake_vector<Float> pi;
    std::unique_ptr<Float[]> P;
    const std::unique_ptr<Float[]>& c;
    std::bernoulli_distribution dist;
    int m;
    int n;

    solver_random_inequalities_101coeff(
      Random& rng_,
      int m_,
      int n_,
      const std::unique_ptr<Float[]>& c_,
      const std::vector<merged_constraint>& csts)
      : rng(rng_)
      , ap(csts, m_, n_)
      , A(std::make_unique<int[]>(ap.size()))
      , R(std::make_unique<rc_data[]>(compute_reduced_costs_vector_size(csts)))
      , b(std::make_unique<bound[]>(m_))
      , P(std::make_unique<Float[]>(ap.size()))
      , c(c_)
      , dist(0.5)
      , m(m_)
      , n(n_)
    {
        int id = 0;
        for (int i = 0, e = length(csts); i != e; ++i) {
            int lower = 0, upper = 0;

            for (const auto& cst : csts[i].elements) {
                bx_ensures(std::abs(cst.factor) == 1);
                A[id++] = cst.factor;

                if (cst.factor > 0)
                    upper++;
                else
                    lower++;
            }

            if (csts[i].min == csts[i].max) {
                b[i].min = csts[i].min;
                b[i].max = csts[i].max;
            } else {
                b[i].min = std::max(-lower, csts[i].min);
                b[i].max = std::min(upper, csts[i].max);
            }

            bx_ensures(b[i].min <= b[i].max);
        }
    }

    void reset() const noexcept
    {
        std::fill(P.get(), P.get() + ap.length(), Float{ 0 });
    }

    int factor(int value) const noexcept
    {
        return A[value];
    }

    int bound_min(int constraint) const noexcept
    {
        return b[constraint].min;
    }

    int bound_max(int constraint) const noexcept
    {
        return b[constraint].max;
    }

    int bound_init(int constraint) const
    {
        return bound_init(constraint, Mode());
    }

    int bound_init(int constraint, minimize_tag) const
    {
        return b[constraint].min;
    }

    int bound_init(int constraint, maximize_tag) const
    {
        return b[constraint].max;
    }

    void decrease_preference(sparse_matrix<int>::row_iterator begin,
                             sparse_matrix<int>::row_iterator end,
                             Float theta) noexcept
    {
        for (; begin != end; ++begin)
            P[begin->value] *= theta;
    }

    template<typename Xtype, typename Iterator>
    void push_and_compute_update_row(Xtype& x,
                                     Iterator first,
                                     Iterator last,
                                     Float kappa,
                                     Float delta,
                                     Float theta,
                                     Float /*obj_amp*/)
    {
        compute_update_row(x, first, last, kappa, delta, theta);
    }

    template<typename Xtype, typename Iterator>
    void compute_update_row(Xtype& x,
                            Iterator first,
                            Iterator last,
                            Float /*kappa*/,
                            Float delta,
                            Float theta)
    {
        for (; first != last; ++first) {
            auto k = constraint(first);
            bx_expects(k < m);

            auto [row_begin, row_end] = ap.row(k);
            decrease_preference(row_begin, row_end, theta);

            int r_size = 0;
            for (auto it = row_begin; it != row_end; ++it, ++r_size) {
                Float sum_a_p = 0;

                auto [col_begin, col_end] = ap.column(it->column);
                for (; col_begin != col_end; ++col_begin)
                    sum_a_p += static_cast<Float>(A[col_begin->value]) *
                               P[col_begin->value];

                R[r_size].id = r_size;
                R[r_size].a = A[it->value];
                R[r_size].value = c[row_begin->column] - sum_a_p;
            }

            calculator_sort<Mode>(R.get(), R.get() + r_size, rng);

            int value = 0;
            bool valid = b[k].min <= 0;
            int i = -1;

            if (!valid) {
                do {
                    ++i;
                    value += R[i].a;
                    valid = b[k].min <= value;
                    auto var = row_begin + R[i].id;
                    x.set(var->column, true);
                    P[var->value] += R[i].a >= 0 ? delta : -delta;
                } while (!valid && i + 1 < r_size);
            }

            valid = b[k].min <= value && value <= b[k].max;
            while (i + 1 < r_size && valid) {
                ++i;
                value += R[i].a;
                valid = b[k].min <= value && value <= b[k].max;
                auto var = row_begin + R[i].id;

                if (valid) {
                    x.set(var->column, true);
                    P[var->value] += R[i].a >= 0 ? delta : -delta;
                } else {
                    x.set(var->column, false);
                    P[var->value] -= R[i].a >= 0 ? delta : -delta;
                }
            }

            while (i + 1 < r_size) {
                ++i;
                auto var = row_begin + R[i].id;
                x.set(var->column, false);
                P[var->value] -= R[i].a >= 0 ? delta : -delta;
            }

            bx_expects(is_valid_constraint(*this, k, x));
        }
    }
};

template<typename Solver,
         typename Float,
         typename Mode,
         typename Order,
         typename Random>
static result
solve_or_optimize(const context_ptr& ctx,
                  const problem& pb,
                  bool is_optimization)
{
    return is_optimization
             ? optimize_problem<Solver, Float, Mode, Order, Random>(ctx, pb)
             : solve_problem<Solver, Float, Mode, Order, Random>(ctx, pb);
}

template<typename Float, typename Mode, typename Random>
static result
select_order(const context_ptr& ctx, const problem& pb, bool is_optimization)
{
    switch (ctx->parameters.order) {
    case solver_parameters::constraint_order::none:
        return solve_or_optimize<
          solver_random_inequalities_101coeff<Float, Mode, Random>,
          Float,
          Mode,
          constraint_sel<Float, Random, 0>,
          Random>(ctx, pb, is_optimization);
    case solver_parameters::constraint_order::reversing:
        return solve_or_optimize<
          solver_random_inequalities_101coeff<Float, Mode, Random>,
          Float,
          Mode,
          constraint_sel<Float, Random, 1>,
          Random>(ctx, pb, is_optimization);
    case solver_parameters::constraint_order::random_sorting:
        return solve_or_optimize<
          solver_random_inequalities_101coeff<Float, Mode, Random>,
          Float,
          Mode,
          constraint_sel<Float, Random, 2>,
          Random>(ctx, pb, is_optimization);
    case solver_parameters::constraint_order::infeasibility_decr:
        return solve_or_optimize<
          solver_random_inequalities_101coeff<Float, Mode, Random>,
          Float,
          Mode,
          constraint_sel<Float, Random, 3>,
          Random>(ctx, pb, is_optimization);
    case solver_parameters::constraint_order::infeasibility_incr:
        return solve_or_optimize<
          solver_random_inequalities_101coeff<Float, Mode, Random>,
          Float,
          Mode,
          constraint_sel<Float, Random, 4>,
          Random>(ctx, pb, is_optimization);
    case solver_parameters::constraint_order::lagrangian_decr:
        return solve_or_optimize<
          solver_random_inequalities_101coeff<Float, Mode, Random>,
          Float,
          Mode,
          constraint_sel<Float, Random, 5>,
          Random>(ctx, pb, is_optimization);
    case solver_parameters::constraint_order::lagrangian_incr:
        return solve_or_optimize<
          solver_random_inequalities_101coeff<Float, Mode, Random>,
          Float,
          Mode,
          constraint_sel<Float, Random, 6>,
          Random>(ctx, pb, is_optimization);
    default:
        bx_reach();
    }
}

template<typename Float, typename Mode>
static result
select_random(const context_ptr& ctx, const problem& pb, bool is_optimization)
{
    return select_order<Float, Mode, std::default_random_engine>(
      ctx, pb, is_optimization);
}

template<typename Float>
static result
select_mode(const context_ptr& ctx, const problem& pb, bool is_optimization)
{
    const auto m = static_cast<int>(pb.type);

    return m == 0
             ? select_random<Float, mode_sel<0>>(ctx, pb, is_optimization)
             : select_random<Float, mode_sel<1>>(ctx, pb, is_optimization);
}

static result
select_float(const context_ptr& ctx, const problem& pb, bool is_optimization)
{
    const auto f = static_cast<int>(ctx->parameters.float_type);

    if (f == 0)
        return select_mode<float_sel<0>>(ctx, pb, is_optimization);
    else if (f == 1)
        return select_mode<float_sel<1>>(ctx, pb, is_optimization);
    else
        return select_mode<float_sel<2>>(ctx, pb, is_optimization);
}

result
solve_random_inequalities_101(const context_ptr& ctx, const problem& pb)
{
    info(ctx, "  - random::solver_inequalities_101coeff\n");
    return select_float(ctx, pb, false);
}

result
optimize_random_inequalities_101(const context_ptr& ctx, const problem& pb)
{
    info(ctx, "  - random::solver_inequalities_101coeff\n");
    return select_float(ctx, pb, true);
}

result
solve_random_equalities_101(const context_ptr& ctx, const problem& pb)
{
    info(ctx, "  - random::solver_equalities_101coeff\n");
    return select_float(ctx, pb, false);
}

result
optimize_random_equalities_101(const context_ptr& ctx, const problem& pb)
{
    info(ctx, "  - random::solver_equalities_101coeff\n");
    return select_float(ctx, pb, true);
}

result
solve_random_inequalities_01(const context_ptr& ctx, const problem& pb)
{
    info(ctx, "  - random::solver_inequalities_01coeff\n");
    return select_float(ctx, pb, false);
}

result
optimize_random_inequalities_01(const context_ptr& ctx, const problem& pb)
{
    info(ctx, "  - random::solver_inequalities_01coeff\n");
    return select_float(ctx, pb, true);
}

result
solve_random_equalities_01(const context_ptr& ctx, const problem& pb)
{
    info(ctx, "  - random::solver_equalities_01coeff\n");
    return select_float(ctx, pb, false);
}

result
optimize_random_equalities_01(const context_ptr& ctx, const problem& pb)
{
    info(ctx, "  - random::solver_equalities_01coeff\n");
    return select_float(ctx, pb, true);
}

} // namespace itm
} // namespace baryonyx
