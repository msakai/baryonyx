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

#include "itm-solver-common.hpp"

#include <memory>

namespace baryonyx {
namespace itm {

template<typename floatingpointT, typename modeT, typename randomT>
struct solver_inequalities_101coeff_buffered
{
    using floatingpoint_type = floatingpointT;
    using mode_type = modeT;
    using random_type = randomT;

    random_type& rng;

    sparse_matrix<int> ap;
    std::vector<bool> x;
    std::unique_ptr<floatingpointT[]> P;
    std::unique_ptr<int[]> A;
    std::unique_ptr<r_data<floatingpoint_type>[]> R;
    fixed_array<fixed_array<c_data>> C;
    std::unique_ptr<bound[]> b;
    std::unique_ptr<floatingpointT[]> pi;

    std::unique_ptr<std::tuple<floatingpointT, floatingpointT>[]> sum_ap;

    const std::unique_ptr<floatingpointT[]>& c;
    int m;
    int n;

    solver_inequalities_101coeff_buffered(
      random_type& rng_,
      int m_,
      int n_,
      const std::unique_ptr<floatingpointT[]>& c_,
      const std::vector<merged_constraint>& csts,
      solver_parameters::init_policy_type init_type,
      double init_random)
      : rng(rng_)
      , ap(csts, m_, n_)
      , x(n_)
      , P(std::make_unique<floatingpointT[]>(ap.size()))
      , A(std::make_unique<int[]>(ap.size()))
      , R(std::make_unique<r_data<floatingpoint_type>[]>(
          compute_reduced_costs_vector_size(csts)))
      , C(m_)
      , b(std::make_unique<bound[]>(m_))
      , pi(std::make_unique<floatingpointT[]>(m_))
      , sum_ap(
          std::make_unique<std::tuple<floatingpointT, floatingpointT>[]>(n_))
      , c(c_)
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

            if (lower > 0) {
                C[i] = fixed_array<c_data>(lower);

                int id_in_r = 0;
                int id_in_c = 0;

                typename sparse_matrix<int>::const_row_iterator it, et;
                std::tie(it, et) = ap.row(i);

                for (; it != et; ++it) {
                    if (A[it->value] < 0) {
                        C[i][id_in_c].id_r = id_in_r;
                        ++id_in_c;
                    }
                    ++id_in_r;
                }
            }
        }

        x_type empty;
        init_solver(*this, empty, init_type, init_random);
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
        return bound_init(constraint, modeT());
    }

    int bound_init(int constraint, minimize_tag) const
    {
        return b[constraint].min;
    }

    int bound_init(int constraint, maximize_tag) const
    {
        return b[constraint].max;
    }

    floatingpointT compute_sum_A_pi(int variable) const
    {
        floatingpointT ret{ 0 };

        sparse_matrix<int>::const_col_iterator ht, hend;
        std::tie(ht, hend) = ap.column(variable);

        for (; ht != hend; ++ht)
            ret += pi[ht->row];

        return ret;
    }

    bool is_valid_solution() const
    {
        for (int k = 0; k != m; ++k) {
            typename sparse_matrix<int>::const_row_iterator it, et;

            std::tie(it, et) = ap.row(k);
            int v = 0;

            for (; it != et; ++it)
                v += A[it->value] * x[it->column];

            if (!(b[k].min <= v && v <= b[k].max))
                return false;
        }

        return true;
    }

    template<typename Container>
    int compute_violated_constraints(Container& container) const
    {
        typename sparse_matrix<int>::const_row_iterator it, et;

        container.clear();

        for (int k = 0; k != m; ++k) {
            std::tie(it, et) = ap.row(k);
            int v = 0;

            for (; it != et; ++it)
                v += A[it->value] * x[it->column];

            if (!(b[k].min <= v && v <= b[k].max))
                container.emplace_back(k);
        }

        return length(container);
    }

    double results(const std::unique_ptr<floatingpointT[]>& original_costs,
                   const double cost_constant) const
    {
        bx_expects(is_valid_solution());

        auto value = static_cast<double>(cost_constant);

        for (int i{ 0 }, ei{ n }; i != ei; ++i)
            value += static_cast<double>(original_costs[i] * x[i]);

        return value;
    }

    void compute_update_row_01_eq(int k,
                                  int bk,
                                  floatingpoint_type kappa,
                                  floatingpoint_type delta,
                                  floatingpoint_type objective_amplifier)
    {
        typename sparse_matrix<int>::row_iterator it, et;
        std::tie(it, et) = ap.row(k);
        const int r_size = compute_reduced_costs(it, et);

        //
        // Before sort and select variables, we apply the push method: for each
        // reduces cost, we had the cost multiply with an objective amplifier.
        //

        if (objective_amplifier)
            for (int i = 0; i != r_size; ++i)
                R[i].value += objective_amplifier * c[(it + R[i].id)->column];

        calculator_sort(R.get(), R.get() + r_size, rng, mode_type());

        int selected = select_variables_equality(r_size, bk);

        affect_variables(it, k, selected, r_size, kappa, delta);
    }

    void compute_update_row_01_ineq(int k,
                                    int bkmin,
                                    int bkmax,
                                    floatingpoint_type kappa,
                                    floatingpoint_type delta,
                                    floatingpoint_type objective_amplifier)
    {
        typename sparse_matrix<int>::row_iterator it, et;
        std::tie(it, et) = ap.row(k);
        const int r_size = compute_reduced_costs(it, et);

        //
        // Before sort and select variables, we apply the push method: for each
        // reduces cost, we had the cost multiply with an objective amplifier.
        //

        if (objective_amplifier)
            for (int i = 0; i != r_size; ++i)
                R[i].value += objective_amplifier * c[(it + R[i].id)->column];

        calculator_sort(R.get(), R.get() + r_size, rng, mode_type());

        int selected = select_variables_inequality(r_size, bkmin, bkmax);

        affect_variables(it, k, selected, r_size, kappa, delta);
    }

    void compute_update_row_101_eq(int k,
                                   int bk,
                                   floatingpoint_type kappa,
                                   floatingpoint_type delta,
                                   floatingpoint_type objective_amplifier)
    {
        typename sparse_matrix<int>::row_iterator it, et;
        std::tie(it, et) = ap.row(k);
        const int r_size = compute_reduced_costs(it, et);
        const auto& ck = C[k];
        const int c_size = length(ck);

        //
        // Before sort and select variables, we apply the push method: for each
        // reduces cost, we had the cost multiply with an objective amplifier.
        //

        if (objective_amplifier)
            for (int i = 0; i != r_size; ++i)
                R[i].value += objective_amplifier * c[(it + R[i].id)->column];

        //
        // Negate reduced costs and coefficients of these variables. We need to
        // parse the row Ak[i] because we need to use r[i] not available in C.
        //

        for (int i = 0; i != c_size; ++i) {
            R[ck[i].id_r].value = -R[ck[i].id_r].value;

            auto var = it + ck[i].id_r;

            P[var->value] = -P[var->value];
        }

        bk += c_size;

        calculator_sort(R.get(), R.get() + r_size, rng, mode_type());

        int selected = select_variables_equality(r_size, bk);

        affect_variables(it, k, selected, r_size, kappa, delta);

        //
        // Clean up: correct negated costs and adjust value of negated
        // variables.
        //

        for (int i = 0; i != c_size; ++i) {
            auto var = it + ck[i].id_r;

            P[var->value] = -P[var->value];
            x[var->column] = !x[var->column];
        }
    }

    void compute_update_row_101_ineq(int k,
                                     int bkmin,
                                     int bkmax,
                                     floatingpoint_type kappa,
                                     floatingpoint_type delta,
                                     floatingpoint_type objective_amplifier)
    {
        typename sparse_matrix<int>::row_iterator it, et;
        std::tie(it, et) = ap.row(k);

        const int r_size = compute_reduced_costs(it, et);
        const auto& ck = C[k];
        const int c_size = (ck) ? length(ck) : 0;

        //
        // Before sort and select variables, we apply the push method: for each
        // reduces cost, we had the cost multiply with an objective amplifier.
        //

        if (objective_amplifier)
            for (int i = 0; i != r_size; ++i)
                R[i].value += objective_amplifier * c[(it + R[i].id)->column];

        //
        // Negate reduced costs and coefficients of these variables. We need to
        // parse the row Ak[i] because we need to use r[i] not available in C.
        //

        for (int i = 0; i != c_size; ++i) {
            R[ck[i].id_r].value = -R[ck[i].id_r].value;

            auto var = it + ck[i].id_r;

            P[var->value] = -P[var->value];
        }

        bkmin += c_size;
        bkmax += c_size;

        calculator_sort(R.get(), R.get() + r_size, rng, mode_type());

        int selected = select_variables_inequality(r_size, bkmin, bkmax);

        affect_variables(it, k, selected, r_size, kappa, delta);

        //
        // Clean up: correct negated costs and adjust value of negated
        // variables.
        //

        for (int i = 0; i != c_size; ++i) {
            auto var = it + ck[i].id_r;

            P[var->value] = -P[var->value];
            x[var->column] = !x[var->column];
        }
    }

    //
    // Compute the reduced costs and return the size of the newly R vector.
    //
    template<typename iteratorT>
    int compute_reduced_costs(iteratorT begin, iteratorT end) noexcept
    {
        int r_size = 0;

        for (; begin != end; ++begin) {
            R[r_size].id = r_size;
            R[r_size].value = c[begin->column] -
                              std::get<0>(sum_ap[begin->column]) -
                              std::get<1>(sum_ap[begin->column]);
            ++r_size;
        }

        return r_size;
    }

    int select_variables_equality(const int r_size, int bk)
    {
        bk = std::min(bk, r_size);

        return bk - 1;
    }

    int select_variables_inequality(const int r_size, int bkmin, int bkmax)
    {
        bkmin = std::min(bkmin, r_size);
        bkmax = std::min(bkmax, r_size);

        for (int i = bkmin; i != bkmax; ++i)
            if (stop_iterating(R[i].value, rng, mode_type()))
                return i - 1;

        return bkmax - 1;
    }

    //
    // The bkmin and bkmax constraint bounds are not equal and can be assigned
    // to -infinity or +infinity. We have to scan the r vector and search a
    // value j such as b(0, k) <= Sum A(k, R[j]) < b(1, k).
    //
    template<typename Iterator>
    void affect_variables(Iterator it,
                          int k,
                          int selected,
                          int r_size,
                          const floatingpointT kappa,
                          const floatingpointT delta) noexcept
    {
        if (selected < 0) {
            for (int i = 0; i != r_size; ++i) {
                auto var = it + R[i].id;

                x[var->column] = false;
                P[var->value] -= delta;
            }
        } else if (selected + 1 >= r_size) {
            pi[k] += R[selected].value;

            for (int i = 0; i != r_size; ++i) {
                auto var = it + R[i].id;

                x[var->column] = true;
                P[var->value] += delta;
            }
        } else {
            pi[k] += ((R[selected].value + R[selected + 1].value) /
                      static_cast<floatingpoint_type>(2.0));

            floatingpoint_type d =
              delta +
              ((kappa / (static_cast<floatingpoint_type>(1.0) - kappa)) *
               (R[selected + 1].value - R[selected].value));

            int i = 0;
            for (; i <= selected; ++i) {
                auto var = it + R[i].id;

                x[var->column] = true;
                P[var->value] += d;
            }

            for (; i != r_size; ++i) {
                auto var = it + R[i].id;

                x[var->column] = false;
                P[var->value] -= d;
            }
        }
    }

    template<typename Iterator>
    void push_and_compute_update_row(Iterator first,
                                     Iterator last,
                                     floatingpoint_type kappa,
                                     floatingpoint_type delta,
                                     floatingpoint_type theta,
                                     floatingpoint_type obj_amp)
    {
        for (int i = 0; i != n; ++i)
            sum_ap[i] = std::make_tuple(static_cast<floatingpointT>(0),
                                        static_cast<floatingpointT>(0));

        std::transform(P.get(),
                       P.get() + ap.length(),
                       P.get(),
                       std::bind1st(std::multiplies<floatingpointT>(), theta));

        for (auto it = first; it != last; ++it) {
            auto k = constraint(it);

            sparse_matrix<int>::row_iterator rit, ret;
            std::tie(rit, ret) = ap.row(k);

            for (; rit != ret; ++rit) {
                if (std::get<0>(sum_ap[rit->column]) == 0 &&
                    std::get<1>(sum_ap[rit->column]) == 0) {
                    sparse_matrix<int>::const_col_iterator ht, hend;
                    std::tie(ht, hend) = ap.column(rit->column);

                    for (; ht != hend; ++ht) {
                        auto f = A[ht->value];
                        auto a = static_cast<floatingpoint_type>(f);

                        std::get<0>(sum_ap[rit->column]) += a * pi[ht->row];
                        std::get<1>(sum_ap[rit->column]) += a * P[ht->value];
                    }
                }
            }
        }

        for (auto it = first; it != last; ++it) {
            auto k = constraint(it);

            if (!C[k]) {
                if (b[k].min == b[k].max)
                    compute_update_row_01_eq(
                      k, b[k].min, kappa, delta, obj_amp);
                else
                    compute_update_row_01_ineq(
                      k, b[k].min, b[k].max, kappa, delta, obj_amp);
            } else {
                if (b[k].min == b[k].max)
                    compute_update_row_101_eq(
                      k, b[k].min, kappa, delta, obj_amp);
                else
                    compute_update_row_101_ineq(
                      k, b[k].min, b[k].max, kappa, delta, obj_amp);
            }
        }
    }

    template<typename Iterator>
    void compute_update_row(Iterator first,
                            Iterator last,
                            floatingpoint_type kappa,
                            floatingpoint_type delta,
                            floatingpoint_type theta)
    {
        for (int i = 0; i != n; ++i)
            sum_ap[i] = std::make_tuple(static_cast<floatingpointT>(0),
                                        static_cast<floatingpointT>(0));

        std::transform(P.get(),
                       P.get() + ap.length(),
                       P.get(),
                       std::bind1st(std::multiplies<floatingpointT>(), theta));

        for (auto it = first; it != last; ++it) {
            auto k = constraint(it);

            sparse_matrix<int>::row_iterator rit, ret;
            std::tie(rit, ret) = ap.row(k);

            for (; rit != ret; ++rit) {
                if (std::get<0>(sum_ap[rit->column]) == 0 &&
                    std::get<1>(sum_ap[rit->column]) == 0) {
                    sparse_matrix<int>::const_col_iterator ht, hend;
                    std::tie(ht, hend) = ap.column(rit->column);

                    for (; ht != hend; ++ht) {
                        auto f = A[ht->value];
                        auto a = static_cast<floatingpoint_type>(f);

                        std::get<0>(sum_ap[rit->column]) += a * pi[ht->row];
                        std::get<1>(sum_ap[rit->column]) += a * P[ht->value];
                    }
                }
            }
        }

        for (auto it = first; it != last; ++it) {
            auto k = constraint(it);

            if (!C[k]) {
                if (b[k].min == b[k].max)
                    compute_update_row_01_eq(
                      k,
                      b[k].min,
                      kappa,
                      delta,
                      static_cast<floatingpoint_type>(0));
                else
                    compute_update_row_01_ineq(
                      k,
                      b[k].min,
                      b[k].max,
                      kappa,
                      delta,
                      static_cast<floatingpoint_type>(0));
            } else {
                if (b[k].min == b[k].max)
                    compute_update_row_101_eq(
                      k,
                      b[k].min,
                      kappa,
                      delta,
                      static_cast<floatingpoint_type>(0));
                else
                    compute_update_row_101_ineq(
                      k,
                      b[k].min,
                      b[k].max,
                      kappa,
                      delta,
                      static_cast<floatingpoint_type>(0));
            }
        }
    }
};

result
solve_inequalities_101coeff_buffered(const context_ptr& ctx, const problem& pb)
{
    info(ctx, "solver: inequalities-101coeff-buffered\n");

    using random_type = std::default_random_engine;

    return select_solver_parameters<solver_inequalities_101coeff_buffered,
                                    random_type>(ctx, pb);
}

result
optimize_inequalities_101coeff_buffered(const context_ptr& ctx,
                                        const problem& pb)
{
    info(ctx, "optimizer: inequalities-101coeff-buffered\n");

    using random_type = std::default_random_engine;

    return select_optimizer_parameters<solver_inequalities_101coeff_buffered,
                                       random_type>(ctx, pb);
}

} // namespace itm
} // namespace baryonyx
