/* Copyright (C) 2018 INRA
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

#ifndef ORG_VLEPROJECT_BARYONYX_PNM_OBSERVER_HPP
#define ORG_VLEPROJECT_BARYONYX_PNM_OBSERVER_HPP

#include "pnm.hpp"
#include "sparse-matrix.hpp"

#include <algorithm>
#include <memory>

namespace baryonyx {

template<typename solverT, typename floatingpointT>
class none_observer
{
public:
    none_observer(const solverT& /*slv*/,
                  std::string /*basename*/,
                  int /*loop*/)
    {}

    void make_observation()
    {}
};

namespace details {

template<typename floatingpointT>
class pi_pnm_observer
{
private:
    const std::unique_ptr<floatingpointT[]>& m_pi;
    int m_len;
    int m_loop;
    pnm_vector m_pnm;

public:
    pi_pnm_observer(std::string filename,
                    const std::unique_ptr<floatingpointT[]>& pi,
                    int len,
                    int loop)
      : m_pi(pi)
      , m_len(len)
      , m_loop(loop)
      , m_pnm(fmt::format("{}-pi.pnm", filename), loop, len)
    {}

    void make_observation()
    {
        std::transform(m_pi.get(),
                       m_pi.get() + m_len,
                       m_pnm.begin(),
                       colormap<floatingpointT>(-5.0, +5.0));

        m_pnm.flush();
    }
};

template<typename floatingpointT>
class ap_pnm_observer
{
private:
    std::string m_basename;
    const sparse_matrix<int> m_ap;
    const std::unique_ptr<floatingpointT[]>& m_P;
    int m_m;
    int m_n;
    int m_frame;

public:
    ap_pnm_observer(std::string basename,
                    const sparse_matrix<int>& ap,
                    const std::unique_ptr<floatingpointT[]>& P,
                    int m,
                    int n)
      : m_basename(basename)
      , m_ap(ap)
      , m_P(P)
      , m_m(m)
      , m_n(n)
      , m_frame(0)
    {}

    void make_observation()
    {
        colormap_2<floatingpointT> cm(-10.0, 0.0, +10.0);
        pnm_array pnm(m_m, m_n);
        if (not pnm)
            return;

        for (int k = 0; k != m_m; ++k) {
            sparse_matrix<int>::const_row_iterator it, et;
            std::tie(it, et) = m_ap.row(k);

            for (; it != et; ++it) {
                std::uint8_t* pointer = pnm(k, it->column);

                auto array = cm(m_P[it->value]);

                *pointer = array[0];
                *(pointer + 1) = array[1];
                *(pointer + 2) = array[2];
            }
        }

        pnm(fmt::format("{}-P-{}.pnm", m_basename, m_frame++));
    }
};
}

template<typename solverT, typename floatingpointT>
class pnm_observer
{
    details::pi_pnm_observer<floatingpointT> m_pi_obs;
    details::ap_pnm_observer<floatingpointT> m_ap_obs;

public:
    pnm_observer(const solverT& slv, std::string basename, int loop)
      : m_pi_obs(basename, slv.pi, slv.m, loop)
      , m_ap_obs(basename, slv.ap, slv.P, slv.m, slv.n)
    {}

    void make_observation()
    {
        m_pi_obs.make_observation();
        m_ap_obs.make_observation();
    }
};

} // namespace baryonyx

#endif
