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

#ifndef ORG_VLEPROJECT_BARYONYX_SOLVER_INEQUALITIES_ZCOEFF_HPP
#define ORG_VLEPROJECT_BARYONYX_SOLVER_INEQUALITIES_ZCOEFF_HPP

#include "branch-and-bound-solver.hpp"
#include "itm-solver-common.hpp"
#include "knapsack-dp-solver.hpp"
#include "sparse-matrix.hpp"

namespace baryonyx {
namespace itm {

template<typename floatingpointT, typename modeT, typename randomT>
struct solver_inequalities_Zcoeff
{
    using floatingpoint_type = floatingpointT;
    using mode_type = modeT;
    using random_type = randomT;

    using AP_type = sparse_matrix<int>;
    using b_type = baryonyx::fixed_array<bound>;
    using c_type = baryonyx::fixed_array<floatingpointT>;
    using pi_type = baryonyx::fixed_array<floatingpointT>;
    using A_type = fixed_array<int>;
    using P_type = fixed_array<floatingpointT>;

    random_type& rng;

    // Sparse matrix to store A and P values.
    AP_type ap;
    A_type A;
    P_type P;

    // Vector shared between all constraints to store the reduced cost.
    fixed_array<r_data<floatingpoint_type>> R;

    // Vector for each constraint with negative coefficients.
    fixed_array<fixed_array<c_data>> C;

    // Vector of boolean where true informs a Z coefficient in the equation or
    // inequation.
    std::vector<bool> Z;

    // Bound vector.
    b_type b;
    const c_type& c;
    x_type x;
    pi_type pi;
    int m;
    int n;

    solver_inequalities_Zcoeff(random_type& rng_,
                               int n_,
                               const c_type& c_,
                               const std::vector<itm::merged_constraint>& csts,
                               itm::init_policy_type init_type,
                               double init_random)
      : rng(rng_)
      , ap(csts, length(csts), n_)
      , A(element_number(csts), 0)
      , P(element_number(csts), 0)
      , C(length(csts))
      , Z(length(csts), false)
      , b(length(csts))
      , c(c_)
      , x(n_)
      , pi(length(csts))
      , m(length(csts))
      , n(n_)
    {
        {
            // Compute the minimal bounds for each constraints, default
            // constraints are -oo <= ... <= bkmax, bkmin <= ... <= +oo and
            // bkmin <= ... <= bkmax. This code remove infinity and replace
            // with minimal or maximal value of the constraint.

            for (int i = 0, e = length(csts); i != e; ++i) {
                int lower = 0, upper = 0;

                for (const auto& cst : csts[i].elements) {
                    if (cst.factor > 0)
                        upper += cst.factor;

                    if (cst.factor < 0)
                        lower += cst.factor;

                    if (std::abs(cst.factor) > 1)
                        Z[i] = true;
                }

                if (csts[i].min == csts[i].max) {
                    b(i).min = csts[i].min;
                    b(i).max = csts[i].max;
                } else {
                    if (csts[i].min == std::numeric_limits<int>::min()) {
                        b(i).min = lower;
                    } else {
                        if (lower < 0)
                            b(i).min = std::max(lower, csts[i].min);
                        else
                            b(i).min = csts[i].min;
                    }

                    if (csts[i].max == std::numeric_limits<int>::max()) {
                        b(i).max = upper;
                    } else {
                        b(i).max = csts[i].max;
                    }
                }
            }
        }

        {
            //
            // Compute the R vector size and the C vectors for each constraints
            // with negative coefficient.
            //

            int rsizemax = 0;
            int id = 0;

            for (int i = 0, e = length(csts); i != e; ++i) {
                int rsize = 0, csize = 0;

                for (const auto& cst : csts[i].elements) {
                    A[id] = cst.factor;
                    ++id;

                    if (cst.factor < 0)
                        ++csize;
                    ++rsize;
                }

                rsizemax = std::max(rsizemax, rsize);

                if (csize <= 0) // No negative coefficient found in
                    continue;   // constraint, try the next.

                C[i] = fixed_array<c_data>(csize);

                int id_in_r = 0;
                int id_in_c = 0;

                typename AP_type::const_row_iterator it, et;
                std::tie(it, et) = ap.row(i);

                for (; it != et; ++it) {
                    if (A[it->value] < 0) {
                        C[i][id_in_c].id_r = id_in_r;
                        ++id_in_c;
                    }
                    ++id_in_r;
                }
            }

            R = fixed_array<r_data<floatingpoint_type>>(rsizemax);
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

        AP_type::const_col_iterator ht, hend;
        std::tie(ht, hend) = ap.column(variable);

        for (; ht != hend; ++ht)
            ret += std::abs(static_cast<floatingpointT>(A[ht->value])) *
                   pi[ht->row];

        return ret;
    }

    void print(const context_ptr& ctx,
               const std::vector<std::string>& names,
               int print_level) const
    {
        if (print_level <= 0)
            return;

        debug(ctx, "  - X: {} to {}\n", 0, length(x));
        for (int i = 0, e = length(x); i != e; ++i)
            debug(ctx,
                  "    - {} {}={}/c_i:{}\n",
                  i,
                  names[i],
                  static_cast<int>(x[i]),
                  c[i]);
        debug(ctx, "\n");

        for (int k = 0, ek = m; k != ek; ++k) {
            typename AP_type::const_row_iterator it, et;

            std::tie(it, et) = ap.row(k);
            int v = 0;

            for (; it != et; ++it)
                v += A[it->value] * x[it->column];

            bool valid = b(k).min <= v and v <= b(k).max;
            debug(ctx,
                  "C {}:{} (Lmult: {})\n",
                  k,
                  (valid ? "   valid" : "violated"),
                  pi[k]);
        }
    }

    bool is_valid_solution() const
    {
        for (int k = 0, ek = m; k != ek; ++k) {
            typename AP_type::const_row_iterator it, et;

            std::tie(it, et) = ap.row(k);
            int v = 0;

            for (; it != et; ++it)
                v += A[it->value] * x[it->column];

            if (not(b[k].min <= v and v <= b[k].max))
                return false;
        }

        return true;
    }

    template<typename Container>
    int compute_violated_constraints(Container& c) const
    {
        typename AP_type::const_row_iterator it, et;

        c.clear();

        for (int k = 0; k != m; ++k) {
            std::tie(it, et) = ap.row(k);
            int v = 0;

            for (; it != et; ++it)
                v += A[it->value] * x[it->column];

            if (not(b(k).min <= v and v <= b(k).max))
                c.emplace_back(k);
        }

        return length(c);
    }

    double results(const c_type& original_costs,
                   const double cost_constant) const
    {
        assert(is_valid_solution());

        double value = static_cast<double>(cost_constant);

        for (int i{ 0 }, ei{ n }; i != ei; ++i)
            value += static_cast<double>(original_costs[i] * x[i]);

        return value;
    }

    typename AP_type::row_iterator ap_value(typename AP_type::row_iterator it,
                                            int id_in_r)
    {
        return it + id_in_r;
    }

    void compute_update_row_Z_eq(int k,
                                 int bk,
                                 floatingpoint_type kappa,
                                 floatingpoint_type delta,
                                 floatingpoint_type theta,
                                 floatingpoint_type objective_amplifier)
    {
        typename AP_type::row_iterator it, et;
        std::tie(it, et) = ap.row(k);

        decrease_preference(it, et, theta);

        const auto& ck = C[k];
        const int c_size = length(ck);
        const int r_size = compute_reduced_costs(it, et);
        int bk_move = 0;

        //
        // Before sort and select variables, we apply the push method: for each
        // reduces cost, we had the cost multiply with an objective amplifier.
        //

        if (objective_amplifier)
            for (int i = 0; i != r_size; ++i)
                R[i].value +=
                  objective_amplifier * c[ap_value(it, R[i].id)->column];

        //
        // Negate reduced costs and coefficients of these variables. We need to
        // parse the row Ak[i] because we need to use r[i] not available in C.
        //

        for (int i = 0; i != c_size; ++i) {
            R[ck[i].id_r].value = -R[ck[i].id_r].value;

            auto var = ap_value(it, ck[i].id_r);

            P[var->value] = -P[var->value];
            bk_move += A[var->value];
        }

        bk += std::abs(bk_move);

        calculator_sort(R.begin(), R.begin() + r_size, rng, mode_type());

        //
        //
        //

        int selected = select_variables_equality_Z(it, r_size, bk);

        affect_variables(it, k, selected, r_size, kappa, delta);

        //
        // Clean up: correct negated costs and adjust value of negated
        // variables.
        //

        for (int i = 0; i != c_size; ++i) {
            auto var = ap_value(it, ck[i].id_r);

            P[var->value] = -P[var->value];
            x[var->column] = !x[var->column];
        }
    }

    void compute_update_row_Z_ineq(int k,
                                   int bkmin,
                                   int bkmax,
                                   floatingpoint_type kappa,
                                   floatingpoint_type delta,
                                   floatingpoint_type theta,
                                   floatingpoint_type objective_amplifier)
    {
        typename AP_type::row_iterator it, et;
        std::tie(it, et) = ap.row(k);

        decrease_preference(it, et, theta);

        const auto& ck = C[k];
        const int c_size = (ck) ? length(ck) : 0;
        const int r_size = compute_reduced_costs(it, et);
        int bk_move = 0;

        //
        // Before sort and select variables, we apply the push method: for each
        // reduces cost, we had the cost multiply with an objective amplifier.
        //

        if (objective_amplifier)
            for (int i = 0; i != r_size; ++i)
                R[i].value +=
                  objective_amplifier * c[ap_value(it, R[i].id)->column];

        //
        // Negate reduced costs and coefficients of these variables. We need to
        // parse the row Ak[i] because we need to use r[i] not available in C.
        //

        for (int i = 0; i != c_size; ++i) {
            R[ck[i].id_r].value = -R[ck[i].id_r].value;

            auto var = ap_value(it, ck[i].id_r);

            P[var->value] = -P[var->value];
            bk_move += A[var->value];
        }

        bkmin += std::abs(bk_move);
        bkmax += std::abs(bk_move);

        calculator_sort(R.begin(), R.begin() + r_size, rng, mode_type());

        //
        //
        //

        int selected = select_variables_inequality_Z(it, r_size, bkmin, bkmax);

        affect_variables(it, k, selected, r_size, kappa, delta);

        //
        // Clean up: correct negated costs and adjust value of negated
        // variables.
        //

        for (int i = 0; i != c_size; ++i) {
            auto var = ap_value(it, ck[i].id_r);

            P[var->value] = -P[var->value];
            x[var->column] = !x[var->column];
        }
    }

    void compute_update_row_01_eq(int k,
                                  int bk,
                                  floatingpoint_type kappa,
                                  floatingpoint_type delta,
                                  floatingpoint_type theta,
                                  floatingpoint_type objective_amplifier)
    {
        typename AP_type::row_iterator it, et;
        std::tie(it, et) = ap.row(k);

        decrease_preference(it, et, theta);

        const int r_size = compute_reduced_costs(it, et);

        //
        // Before sort and select variables, we apply the push method: for each
        // reduces cost, we had the cost multiply with an objective amplifier.
        //

        if (objective_amplifier)
            for (int i = 0; i != r_size; ++i)
                R[i].value +=
                  objective_amplifier * c[ap_value(it, R[i].id)->column];

        calculator_sort(R.begin(), R.begin() + r_size, rng, mode_type());

        int selected = select_variables_equality(r_size, bk);

        affect_variables(it, k, selected, r_size, kappa, delta);
    }

    void compute_update_row_01_ineq(int k,
                                    int bkmin,
                                    int bkmax,
                                    floatingpoint_type kappa,
                                    floatingpoint_type delta,
                                    floatingpoint_type theta,
                                    floatingpoint_type objective_amplifier)
    {
        typename AP_type::row_iterator it, et;
        std::tie(it, et) = ap.row(k);

        decrease_preference(it, et, theta);

        const int r_size = compute_reduced_costs(it, et);

        //
        // Before sort and select variables, we apply the push method: for each
        // reduces cost, we had the cost multiply with an objective amplifier.
        //

        if (objective_amplifier)
            for (int i = 0; i != r_size; ++i)
                R[i].value +=
                  objective_amplifier * c[ap_value(it, R[i].id)->column];

        calculator_sort(R.begin(), R.begin() + r_size, rng, mode_type());

        int selected = select_variables_inequality(r_size, bkmin, bkmax);

        affect_variables(it, k, selected, r_size, kappa, delta);
    }

    void compute_update_row_101_eq(int k,
                                   int bk,
                                   floatingpoint_type kappa,
                                   floatingpoint_type delta,
                                   floatingpoint_type theta,
                                   floatingpoint_type objective_amplifier)
    {
        typename AP_type::row_iterator it, et;
        std::tie(it, et) = ap.row(k);

        decrease_preference(it, et, theta);

        const auto& ck = C[k];
        const int c_size = length(ck);
        const int r_size = compute_reduced_costs(it, et);

        //
        // Before sort and select variables, we apply the push method: for each
        // reduces cost, we had the cost multiply with an objective amplifier.
        //

        if (objective_amplifier)
            for (int i = 0; i != r_size; ++i)
                R[i].value +=
                  objective_amplifier * c[ap_value(it, R[i].id)->column];

        //
        // Negate reduced costs and coefficients of these variables. We need to
        // parse the row Ak[i] because we need to use r[i] not available in C.
        //

        for (int i = 0; i != c_size; ++i) {
            R[ck[i].id_r].value = -R[ck[i].id_r].value;

            auto var = ap_value(it, ck[i].id_r);

            P[var->value] = -P[var->value];
        }

        bk += c_size;

        calculator_sort(R.begin(), R.begin() + r_size, rng, mode_type());

        int selected = select_variables_equality(r_size, bk);

        affect_variables(it, k, selected, r_size, kappa, delta);

        //
        // Clean up: correct negated costs and adjust value of negated
        // variables.
        //

        for (int i = 0; i != c_size; ++i) {
            auto var = ap_value(it, ck[i].id_r);

            P[var->value] = -P[var->value];
            x[var->column] = !x[var->column];
        }
    }

    void compute_update_row_101_ineq(int k,
                                     int bkmin,
                                     int bkmax,
                                     floatingpoint_type kappa,
                                     floatingpoint_type delta,
                                     floatingpoint_type theta,
                                     floatingpoint_type objective_amplifier)
    {
        typename AP_type::row_iterator it, et;
        std::tie(it, et) = ap.row(k);

        decrease_preference(it, et, theta);

        const auto& ck = C[k];
        const int c_size = (ck) ? length(ck) : 0;
        const int r_size = compute_reduced_costs(it, et);

        //
        // Before sort and select variables, we apply the push method: for each
        // reduces cost, we had the cost multiply with an objective amplifier.
        //

        if (objective_amplifier)
            for (int i = 0; i != r_size; ++i)
                R[i].value +=
                  objective_amplifier * c[ap_value(it, R[i].id)->column];

        //
        // Negate reduced costs and coefficients of these variables. We need to
        // parse the row Ak[i] because we need to use r[i] not available in C.
        //

        for (int i = 0; i != c_size; ++i) {
            R[ck[i].id_r].value = -R[ck[i].id_r].value;

            auto var = ap_value(it, ck[i].id_r);

            P[var->value] = -P[var->value];
        }

        bkmin += c_size;
        bkmax += c_size;

        calculator_sort(R.begin(), R.begin() + r_size, rng, mode_type());

        int selected = select_variables_inequality(r_size, bkmin, bkmax);

        affect_variables(it, k, selected, r_size, kappa, delta);

        //
        // Clean up: correct negated costs and adjust value of negated
        // variables.
        //

        for (int i = 0; i != c_size; ++i) {
            auto var = ap_value(it, ck[i].id_r);

            P[var->value] = -P[var->value];
            x[var->column] = !x[var->column];
        }
    }

    //
    // Decrease influence of local preferences. 0 will completely reset the
    // preference values for the current row. > 0 will keep former decision in
    // mind.
    //
    template<typename iteratorT>
    void decrease_preference(iteratorT begin,
                             iteratorT end,
                             floatingpoint_type theta) noexcept
    {
        for (; begin != end; ++begin)
            P[begin->value] *= theta;
    }

    //
    // Compute the reduced costs and return the size of the newly R vector.
    //
    template<typename iteratorT>
    int compute_reduced_costs(iteratorT begin, iteratorT end) noexcept
    {
        int r_size = 0;

        for (; begin != end; ++begin) {
            floatingpoint_type sum_a_pi = 0;
            floatingpoint_type sum_a_p = 0;

            typename AP_type::const_col_iterator ht, hend;
            std::tie(ht, hend) = ap.column(begin->column);

            for (; ht != hend; ++ht) {
                auto f = A[ht->value];
                auto a = static_cast<floatingpoint_type>(f);

                sum_a_pi += a * pi[ht->row];
                sum_a_p += a * P[ht->value];
            }

            // R[r_size].id = begin->column;
            R[r_size].id = r_size;
            R[r_size].value = c[begin->column] - sum_a_pi - sum_a_p;
            ++r_size;
        }

        return r_size;
    }

    int select_variables_equality(const int r_size, int bk)
    {
        (void)r_size;

        assert(bk <= r_size && "b(k) can not be reached, this is an "
                               "error of the preprocessing step.");

        return bk - 1;
    }

    int select_variables_inequality(const int r_size, int bkmin, int bkmax)
    {
        int i = 0;
        int selected = -1;
        int sum = 0;

        for (; i != r_size; ++i) {
            sum += 1;

            if (bkmin <= sum)
                break;
        }

        assert(bkmin <= sum && "b(0, k) can not be reached, this is an "
                               "error of the preprocessing step.");

        if (bkmin <= sum and sum <= bkmax) {
            selected = i;
            for (; i != r_size; ++i) {
                sum += 1;

                if (sum <= bkmax) {
                    if (stop_iterating(R[i].value, rng, mode_type()))
                        break;
                    ++selected;
                } else
                    break;
            }

            assert(i != r_size && "unrealisable, preprocessing error");
        }

        return selected;
    }

    template<typename iteratorT>
    int select_variables_equality_Z(iteratorT it, const int r_size, int bk)
    {
        int sum = A[ap_value(it, R[0].id)->value];
        if (sum == bk)
            return 0;

        for (int i = 1; i != r_size; ++i) {
            sum += A[ap_value(it, R[i].id)->value];

            if (sum == bk)
                return i;

            if (sum > bk)
                break;
        }

        //
        // If the previous selection failed, we try a branch and bound
        // algorithm to found the solution.
        //

        return knapsack_dp_solver<modeT, floatingpointT>(
          A, R, it, it + r_size, bk);
    }

    template<typename iteratorT>
    int select_variables_inequality_Z(iteratorT it,
                                      const int r_size,
                                      int bkmin,
                                      int bkmax)
    {
        int sum = A[ap_value(it, R[0].id)->value];
        if (bkmin <= sum and sum <= bkmax)
            return 0;

        for (int i = 1; i != r_size; ++i) {
            sum += A[ap_value(it, R[i].id)->value];

            if (bkmin <= sum and sum <= bkmax)
                return i;

            if (sum > bkmax)
                break;
        }

        //
        // If the previous selection failed, we try a branch and bound
        // algorithm to found the solution.
        //

        return knapsack_dp_solver<modeT, floatingpointT>(
          A, R, it, it + r_size, bkmax);
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
                auto var = ap_value(it, R[i].id);

                x[var->column] = 0;
                P[var->value] -= delta;
            }
        } else if (selected + 1 >= r_size) {
            for (int i = 0; i != r_size; ++i) {
                auto var = ap_value(it, R[i].id);

                x[var->column] = 1;
                P[var->value] += delta;
            }
        } else {
            pi(k) += ((R[selected].value + R[selected + 1].value) /
                      static_cast<floatingpoint_type>(2.0));

            floatingpoint_type d =
              delta +
              ((kappa / (static_cast<floatingpoint_type>(1.0) - kappa)) *
               (R[selected + 1].value - R[selected].value));

            int i = 0;
            for (; i <= selected; ++i) {
                auto var = ap_value(it, R[i].id);

                x[var->column] = 1;
                P[var->value] += d;
            }

            for (; i != r_size; ++i) {
                auto var = ap_value(it, R[i].id);

                x[var->column] = 0;
                P[var->value] -= d;
            }
        }
    }

    void push_and_compute_update_row(int k,
                                     floatingpoint_type kappa,
                                     floatingpoint_type delta,
                                     floatingpoint_type theta,
                                     floatingpoint_type obj_amp)
    {
        if (Z[k]) {
            if (b(k).min == b(k).max)
                compute_update_row_Z_eq(
                  k, b(k).min, kappa, delta, theta, obj_amp);
            else
                compute_update_row_Z_ineq(
                  k, b(k).min, b(k).max, kappa, delta, theta, obj_amp);
        } else if (!C[k]) {
            if (b(k).min == b(k).max)
                compute_update_row_01_eq(
                  k, b(k).min, kappa, delta, theta, obj_amp);
            else
                compute_update_row_01_ineq(
                  k, b(k).min, b(k).max, kappa, delta, theta, obj_amp);
        } else {
            if (b(k).min == b(k).max)
                compute_update_row_101_eq(
                  k, b(k).min, kappa, delta, theta, obj_amp);
            else
                compute_update_row_101_ineq(
                  k, b(k).min, b(k).max, kappa, delta, theta, obj_amp);
        }
    }

    void compute_update_row(int k,
                            floatingpoint_type kappa,
                            floatingpoint_type delta,
                            floatingpoint_type theta)
    {
        if (Z[k]) {
            if (b(k).min == b(k).max)
                compute_update_row_Z_eq(k,
                                        b(k).min,
                                        kappa,
                                        delta,
                                        theta,
                                        static_cast<floatingpoint_type>(0));
            else
                compute_update_row_Z_ineq(k,
                                          b(k).min,
                                          b(k).max,
                                          kappa,
                                          delta,
                                          theta,
                                          static_cast<floatingpoint_type>(0));
        } else if (!C[k]) {
            if (b(k).min == b(k).max)
                compute_update_row_01_eq(k,
                                         b(k).min,
                                         kappa,
                                         delta,
                                         theta,
                                         static_cast<floatingpoint_type>(0));
            else
                compute_update_row_01_ineq(k,
                                           b(k).min,
                                           b(k).max,
                                           kappa,
                                           delta,
                                           theta,
                                           static_cast<floatingpoint_type>(0));
        } else {
            if (b(k).min == b(k).max)
                compute_update_row_101_eq(k,
                                          b(k).min,
                                          kappa,
                                          delta,
                                          theta,
                                          static_cast<floatingpoint_type>(0));
            else
                compute_update_row_101_ineq(
                  k,
                  b(k).min,
                  b(k).max,
                  kappa,
                  delta,
                  theta,
                  static_cast<floatingpoint_type>(0));
        }
    }
};

} // namespace itm
} // namespace baryonyx

#endif
