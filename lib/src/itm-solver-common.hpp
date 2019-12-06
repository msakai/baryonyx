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

#ifndef ORG_VLEPROJECT_BARYONYX_SOLVER_ITM_SOLVER_COMMON_HPP
#define ORG_VLEPROJECT_BARYONYX_SOLVER_ITM_SOLVER_COMMON_HPP

#include <baryonyx/core-compare>

#include <algorithm>
#include <chrono>
#include <functional>
#include <utility>
#include <vector>

#include "itm-common.hpp"
#include "observer.hpp"
#include "private.hpp"
#include "sparse-matrix.hpp"
#include "utils.hpp"

namespace baryonyx {
namespace itm {

template<typename Solver,
         typename Float,
         typename Mode,
         typename Cost,
         typename Observer>
struct solver_functor
{
    std::chrono::time_point<std::chrono::steady_clock> m_begin;
    std::chrono::time_point<std::chrono::steady_clock> m_end;

    const context_ptr& m_ctx;
    random_engine& m_rng;

    raw_result<Mode> m_best;

    solver_functor(const context_ptr& ctx, random_engine& rng)
      : m_ctx(ctx)
      , m_rng(rng)
    {}

    result operator()(const std::vector<merged_constraint>& constraints,
                      int variables,
                      const Cost& original_costs,
                      double cost_constant)
    {
        result r;

        bit_array x(variables);

        int best_remaining = INT_MAX;

        auto& p = m_ctx->parameters;

        auto norm_costs = normalize_costs<Float, Cost>(
          m_ctx, original_costs, m_rng, variables);

        const auto kappa_step = static_cast<Float>(p.kappa_step);
        const auto kappa_max = static_cast<Float>(p.kappa_max);
        const auto alpha = static_cast<Float>(p.alpha);
        const auto theta = static_cast<Float>(p.theta);
        const auto delta =
          p.delta < 0
            ? compute_delta<Float, Cost>(m_ctx, norm_costs, theta, variables)
            : static_cast<Float>(p.delta);

        const auto pushing_k_factor = static_cast<Float>(p.pushing_k_factor);
        const auto pushing_objective_amplifier =
          static_cast<Float>(p.pushing_objective_amplifier);

        const int w_limit = static_cast<int>(p.w);

        if (p.limit <= 0)
            p.limit = std::numeric_limits<int>::max();

        if (p.time_limit <= 0)
            p.time_limit = std::numeric_limits<double>::infinity();

        if (p.pushes_limit <= 0)
            p.pushes_limit = 0;

        if (p.pushing_iteration_limit <= 0)
            p.pushes_limit = 0;

        Solver slv(
          m_rng, length(constraints), variables, norm_costs, constraints);

        compute_order compute(p.order, variables);

        {
            std::bernoulli_distribution choose_mutation(
              m_ctx->parameters.init_random);
            bit_array empty_x;

            switch (p.init_policy) {
            case solver_parameters::init_policy_type::pessimistic_solve:
                init_with_pre_solve<Cost, Mode>(
                  x, empty_x, m_rng, original_costs, constraints);
                break;

            case solver_parameters::init_policy_type::optimistic_solve:
                init_with_pre_solve<Cost, Mode>(
                  empty_x, x, m_rng, original_costs, constraints);
                break;

            case solver_parameters::init_policy_type::bastert:
            case solver_parameters::init_policy_type::cycle:
            case solver_parameters::init_policy_type::crossover_cycle:
                init_with_bastert<Cost, Mode>(x, original_costs, variables, 0);
                break;
            }

            for (int i = 0, e = x.size(); i != e; ++i)
                if (choose_mutation(m_rng))
                    x.invert(i);
        }

        bool start_push = false;
        auto kappa = static_cast<Float>(p.kappa_min);

        Observer obs("img", slv.m, slv.n, p.limit);

        m_begin = std::chrono::steady_clock::now();
        m_end = std::chrono::steady_clock::now();

        compute.init(slv, x);

        for (int i = 0; i != p.limit; ++i) {
            auto remaining = compute.run(slv, x, m_rng, kappa, delta, theta);
            obs.make_observation(slv.ap, slv.P.get(), slv.pi.get());

            if (remaining == 0) {
                store_if_better(
                  x, original_costs.results(x, cost_constant), i);
                best_remaining = remaining;
                start_push = true;
                break;
            }

            if (remaining < best_remaining) {
                store_if_better(x, remaining, i);
                best_remaining = remaining;
            }

            if (i > w_limit)
                kappa += kappa_step * std::pow(static_cast<Float>(remaining) /
                                                 static_cast<Float>(slv.m),
                                               alpha);

            if (kappa > kappa_max) {
                r.status = result_status::kappa_max_reached;
                break;
            }

            if (is_timelimit_reached()) {
                r.status = result_status::time_limit_reached;
                break;
            }
        }

        if (!start_push) {
            r.status = result_status::limit_reached;
        } else {
            for (int push = 0; push < p.pushes_limit; ++push) {
                auto remaining =
                  compute.push_and_run(slv,
                                       x,
                                       m_rng,
                                       pushing_k_factor * kappa,
                                       delta,
                                       theta,
                                       pushing_objective_amplifier);

                if (remaining == 0)
                    store_if_better(x,
                                    original_costs.results(x, cost_constant),
                                    -push * p.pushing_iteration_limit - 1);

                if (is_timelimit_reached())
                    break;

                for (int iter = 0; iter < p.pushing_iteration_limit; ++iter) {
                    remaining =
                      compute.run(slv, x, m_rng, kappa, delta, theta);

                    if (remaining == 0) {
                        store_if_better(
                          x,
                          original_costs.results(x, cost_constant),
                          -push * p.pushing_iteration_limit - iter - 1);
                        break;
                    }

                    if (iter > p.w)
                        kappa +=
                          kappa_step * std::pow(static_cast<Float>(remaining) /
                                                  static_cast<Float>(slv.m),
                                                alpha);

                    if (kappa > kappa_max)
                        break;

                    if (is_timelimit_reached())
                        break;
                }
            }
        }

        if (m_best.remaining_constraints == 0)
            r.status = result_status::success;

        if (!m_best.x.empty()) {
            r.solutions.resize(1);
            convert(m_best, r.solutions[0], variables);
        }

        return r;
    }

private:
    bool is_timelimit_reached()
    {
        m_end = std::chrono::steady_clock::now();

        return is_time_limit(m_ctx->parameters.time_limit, m_begin, m_end);
    }

    double duration()
    {
        m_end = std::chrono::steady_clock::now();

        return compute_duration(m_begin, m_end);
    }

    void store_if_better(const bit_array& x, int remaining, int i)
    {
        if (m_best.remaining_constraints > remaining) {
            m_best.x = x;
            m_best.duration = duration();
            m_best.loop = i;
            m_best.remaining_constraints = remaining;

            if (m_ctx->update)
                m_ctx->update(remaining, 0.0, i, m_best.duration);
        }
    }

    void store_if_better(const bit_array& x, double current, int i)
    {
        if (is_better_solution<Mode>(current, m_best.value)) {
            m_best.x = x;
            m_best.duration = duration();
            m_best.loop = i;
            m_best.remaining_constraints = 0;
            m_best.value = current;

            if (m_ctx->update)
                m_ctx->update(0, current, i, m_best.duration);
        }
    }
};

template<typename Solver, typename Float, typename Mode, typename Cost>
inline result
solve_problem(const context_ptr& ctx, const problem& pb)
{
    if (ctx->start)
        ctx->start(ctx->parameters);

    result ret;

    auto variables = length(pb.vars.values);
    auto constraints{ make_merged_constraints(ctx, pb) };

    if (!constraints.empty() && !pb.vars.values.empty()) {
        random_engine rng(init_random_generator_seed(ctx));

        auto cost = Cost(pb.objective, variables);
        auto cost_constant = pb.objective.value;

        switch (ctx->parameters.observer) {
        case solver_parameters::observer_type::pnm: {
            using obs = pnm_observer;

            solver_functor<Solver, Float, Mode, Cost, obs> slv(ctx, rng);
            ret = slv(constraints, variables, cost, cost_constant);
        } break;
        case solver_parameters::observer_type::file: {
            using obs = file_observer;

            solver_functor<Solver, Float, Mode, Cost, obs> slv(ctx, rng);
            ret = slv(constraints, variables, cost, cost_constant);
        } break;
        default: {
            using obs = none_observer;

            solver_functor<Solver, Float, Mode, Cost, obs> slv(ctx, rng);
            ret = slv(constraints, variables, cost, cost_constant);
            break;
        }
        }
    } else {
        ret.status = result_status::success;
    }

    ret.strings = pb.strings;
    ret.variable_name = std::move(pb.vars.names);
    ret.affected_vars = std::move(pb.affected_vars);
    ret.variables = variables;
    ret.constraints = length(constraints);

    if (ctx->finish)
        ctx->finish(ret);

    return ret;
}

} // namespace itm
} // namespace baryonyx

#endif
