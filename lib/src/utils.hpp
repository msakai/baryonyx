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

#ifndef ORG_VLEPROJECT_BARYONYX_SOLVER_UTILS_HPP
#define ORG_VLEPROJECT_BARYONYX_SOLVER_UTILS_HPP

#include <baryonyx/core>

#include "private.hpp"

#include <chrono>
#include <functional>
#include <utility>

#include <climits>
#include <cmath>

namespace baryonyx {

////////////////////////////////////////////////

/**
 * @brief Compute the length of the @c container.
 * @details Return the @c size provided by the @c C::size() but cast it into a
 *     @c int. This is a specific baryonyx function, we know that number of
 *         variables and constraints are lower than the @c INT_MAX value.
 *
 * @code
 * std::vector<int> v(z);
 *
 * for (int i = 0, e = length(v); i != e; ++i)
 *     ...
 * @endcode
 *
 * @param c The container to request to size.
 * @tparam T The of container value (must provide a @c size() member).
 */
template<class C>
constexpr int
length(const C& c) noexcept
{
#ifdef NDEBUG
    Expects(c.size() <= static_cast<std::size_t>(INT_MAX),
            "length(): container too big");
#endif

    return static_cast<int>(c.size());
}

/**
 * @details Reference to @c lo if @c v is less than @c lo, reference to @c
 * hi if @c hi is less than @c v, otherwise reference to @c v.
 *
 * @code
 * assert(baryonyx::clamp(0.0, 0.0, 1.0) == 0.0);
 * assert(baryonyx::clamp(1.0, 0.0, 1.0) == 1.0);
 * assert(baryonyx::clamp(-0.5, 0.0, 1.0) == 0.0);
 * assert(baryonyx::clamp(1.5, 0.0, 1.0) == 1.0);
 * assert(baryonyx::clamp(168, -128, +127) == 127);
 * assert(baryonyx::clamp(168, 0, +255) == 168);
 * assert(baryonyx::clamp(128, -128, +127) == 127);
 * assert(baryonyx::clamp(128, 0, +255) == 128);
 * @endcode
 *
 * @param v The value to clamp.
 * @param lo The low value to compare.
 * @param hi The high value to compare.
 * @return value, low or high.
 */
template<class T>
constexpr const T&
clamp(const T& v, const T& lo, const T& hi)
{
#ifdef NDEBUG
    Expects(lo < hi, "clamp(): low > high");
#endif

    return v < lo ? lo : v > hi ? hi : v;
}

/**
 * @brief Compute the length of the C array.
 * @details Return the size of the C array but cast it into a @c int. This
 * is a specific baryonyx function, we know that number of variables and
 *     constraints are lower than the  @c int max value (INT_MAX).
 *
 * @code
 * int v[150];
 * for (int i = 0, e = length(v); i != e; ++i)
 *     ...
 * @endcode
 *
 * @param v The container to return size.
 * @tparam T The type of the C array.
 * @tparam N The size of the C array.
 */
template<class T, size_t N>
constexpr int
length(const T (&array)[N]) noexcept
{
    (void)array;

#ifdef NDEBUG
    Expects(N > static_cast<std::size_t>(INT_MAX),
            "length(): container too big");
#endif

    return static_cast<int>(N);
}

inline void
Expects(bool condition)
{
    if (not condition)
        throw precondition_failure("precondition failure");
}

inline void
Ensures(bool condition)
{
    if (not condition)
        throw postcondition_failure("postcondition failure");
}

inline void
Expects(bool condition, const char* s)
{
    if (not condition)
        throw precondition_failure(s);
}

inline void
Ensures(bool condition, const char* s)
{
    if (not condition)
        throw postcondition_failure(s);
}

template<typename T>
inline bool
is_essentially_equal(const T v1, const T v2, const T epsilon)
{
    static_assert(std::is_floating_point<T>::value,
                  "is_essentially_equal required a float/double "
                  "as template argument");

    return fabs((v1) - (v2)) <=
           ((fabs(v1) > fabs(v2) ? fabs(v2) : fabs(v1)) * (epsilon));
}

/**
 * @brief Check if the duration between @c begin and @c end is greater than
 * @c limit in second.
 *
 * @details This function checks if @c end - @c begin is greater than @c
 * limit.
 *
 * @code
 * auto begin = std::chrono::steady_clock::now();
 * // computation
 * auto end = std::chrono::steady_clock::now();
 *
 * if (is_time_limit(10.0, begin, end)) {
 *    std::cout << "computation takes more than 10s.\n";
 * }
 * @endcode
 *
 * @param limit Minimal duration in second to test.
 * @param begin Time point from the begining of a compuation.
 * @param end Time point of the current compuation.
 */
inline bool
is_time_limit(double limit,
              std::chrono::steady_clock::time_point begin,
              std::chrono::steady_clock::time_point end) noexcept
{
    using std::chrono::duration;
    using std::chrono::duration_cast;

    if (limit <= 0)
        return false;

    return duration_cast<duration<double>>(end - begin).count() > limit;
}

/**
 * @brief @c is_numeric_castable checks if two integer are castable.
 *
 * @details Checks if the @c arg Source integer is castable into @c Target
 *     template. @c Source and @c Target must be integer. The test
 *     includes the limit of @c Target.
 *
 * @param arg The integer to test.
 * @return true if @c arg is castable to @c Target type.
 *
 * @code
 * int v1 = 10;
 * assert(lp::is_numeric_castable<std::int8_t>(v));
 *
 * int v2 = 278;
 * assert(not lp::is_numeric_castable<std::int8_t>(v2));
 * @endcode
 */
template<typename Target, typename Source>
inline bool
is_numeric_castable(Source arg) noexcept
{
    static_assert(std::is_integral<Source>::value, "Integer required.");
    static_assert(std::is_integral<Target>::value, "Integer required.");

    using arg_traits = std::numeric_limits<Source>;
    using result_traits = std::numeric_limits<Target>;

    if (result_traits::digits == arg_traits::digits and
        result_traits::is_signed == arg_traits::is_signed)
        return true;

    if (result_traits::digits > arg_traits::digits)
        return result_traits::is_signed or arg >= 0;

    if (arg_traits::is_signed and
        arg < static_cast<Source>(result_traits::min()))
        return false;

    return arg <= static_cast<Source>(result_traits::max());
}

/**
 * @brief @c numeric_cast cast @c s to @c Target type.
 *
 * @details Converts the integer type @c Source @c s into the integer type
 *     @c Target. If the value @c s is not castable to @c Target, @c
 *     numeric_cast throws an exception.
 *
 * @param s The integer to cast.
 * @return The cast integer.
 *
 * @code
 * std::vector<double> v(1024);
 * long int index = baryonyx::numeric_cast<long int>(v); // No throw.
 * @endcode
 */
template<typename Target, typename Source>
inline Target
numeric_cast(Source s)
{
    if (not is_numeric_castable<Target>(s))
        throw numeric_cast_failure();

    return static_cast<Target>(s);
}

/**
 * @brief A class to compute time spent during object life.
 *
 * @code
 * {
 *    ...
 *    timer t([](double t){std::cout << t << "s."; });
 *    ...
 * } // Show time spend since timer object instantiation.
 * @endcode
 */
class timer
{
private:
    std::chrono::time_point<std::chrono::steady_clock> m_start;
    std::function<void(double)> m_fct;

public:
    /**
     * @brief Build timer with output function.
     *
     * @param fct Output function called in destructor.
     */
    template<typename Function>
    timer(Function fct)
      : m_start(std::chrono::steady_clock::now())
      , m_fct(fct)
    {}

    double time_elapsed() const
    {
        namespace sc = std::chrono;

        auto diff = std::chrono::steady_clock::now() - m_start;
        auto dc = sc::duration_cast<sc::duration<double, std::ratio<1>>>(diff);

        return dc.count();
    }

    ~timer() noexcept
    {
        try {
            if (m_fct)
                m_fct(time_elapsed());
        } catch (...) {
        }
    }
};

} // namespace baryonyx

#endif
