#ifndef INCLUDE_VERSOR_HPP_2F20C605_8C99_4BB5_BF12_B0E3A0DDA6AE
#define INCLUDE_VERSOR_HPP_2F20C605_8C99_4BB5_BF12_B0E3A0DDA6AE

#include <iosfwd>
#include <array>
#include <numeric>
#include <algorithm>
#include <span>
#include <tuple>
#include <cstddef>

namespace aux
{
    template <class T>
    consteval T sqrt( T s ) {
        T x = s / 2.0 ;
        T prev = 0.0 ;
        while ( x != prev ) {
            prev = x ;
            x = (x + s / x ) / 2.0 ;
        }
        return x ;
    }

    template <class T, size_t N>
    union versor {
    public:
        using array_type = std::array<T, N>;
        using value_type = typename array_type::value_type;
        using size_type = typename array_type::size_type;

    public:
        template <size_t M>
        struct reducer : reducer<M-1> {
        public:
            using basis = reducer<M-1>;

        public:
            value_type value;

        public:
            constexpr auto& negate() noexcept {
                basis::negate();
                this->value = -this->value;
                return *this;
            }
            constexpr auto& operator+=(reducer const& rhs) noexcept {
                basis::operator+=(rhs);
                this->value += rhs.value;
                return *this;
            }
            constexpr auto& operator-=(reducer const& rhs) noexcept {
                basis::operator-=(rhs);
                this->value -= rhs.value;
                return *this;
            }
            constexpr auto& operator*=(value_type scalar) noexcept {
                basis::operator*=(scalar);
                this->value *= scalar;
                return *this;
            }
            constexpr auto& operator/=(value_type scalar) noexcept {
                basis::operator/=(scalar);
                this->value /= scalar;
                return *this;
            }
            constexpr auto dot(reducer const& rhs) const noexcept {
                return basis::dot(rhs) + this->value * rhs.value;
            }

        public:
            template <class Ch>
            friend auto& operator<<(std::basic_ostream<Ch>& output, reducer const& v) noexcept {
                output.put('(');
                v.print(output);
                return output.put(')');
            }

        protected:
            template <class Ch>
            void print(std::basic_ostream<Ch>& output) const noexcept {
                basis::print(output);
                if constexpr (M != 1) output.put(' ');
                output << this->value;
            }
        };

        template <>
        struct reducer<0> {
            constexpr auto& negate() noexcept { return *this; }
            constexpr auto& operator+=(reducer const&) noexcept { return *this; }
            constexpr auto& operator-=(reducer const&) noexcept { return *this; }
            constexpr auto& operator*=(value_type) noexcept { return *this; }
            constexpr auto& operator/=(value_type) noexcept { return *this; }
            constexpr auto dot(reducer const&) const noexcept { return value_type(); }

        protected:
            void print(auto&) const noexcept { }
        };

    public:
        constexpr versor(auto... args) noexcept : arr{ static_cast<T>(args)... } { }

        constexpr explicit operator array_type&()       noexcept { return arr; }
        constexpr explicit operator array_type () const noexcept { return arr; }

    public:
        constexpr auto size() const noexcept { return arr.size(); }

        constexpr auto begin()       noexcept { return arr.begin(); }
        constexpr auto begin() const noexcept { return arr.begin(); }
        constexpr auto end()       noexcept { return arr.end(); }
        constexpr auto end() const noexcept { return arr.end(); }

        constexpr auto& operator[](size_type idx)       noexcept { return arr[idx]; }
        constexpr auto  operator[](size_type idx) const noexcept { return arr[idx]; }

    public:
        constexpr auto& negate() noexcept {
            rec.negate();
            return *this;
        }
        constexpr auto& operator+=(versor const& rhs) noexcept {
            if (std::is_constant_evaluated()) {
                for (size_type i = 0; i < size(); ++i) {
                    arr[i] += rhs.arr[i];
                }
            }
            else {
                rec.operator+=(rhs.rec);
            }
            return *this;
        }
        constexpr auto& operator-=(versor const& rhs) noexcept {
            if (std::is_constant_evaluated()) {
                for (size_type i = 0; i < size(); ++i) {
                    arr[i] -= rhs.arr[i];
                }
            }
            else {
                rec.operator-=(rhs.rec);
            }
            return *this;
        }
        constexpr auto& operator*=(value_type scalar) noexcept {
            if (std::is_constant_evaluated()) {
                for (auto& item : arr) {
                    item *= scalar;
                }
            }
            else {
                rec.operator*=(scalar);
            }
            return *this;
        }
        constexpr auto& operator/=(value_type scalar) noexcept {
            if (std::is_constant_evaluated()) {
                for (auto& item : arr) {
                    item /= scalar;
                }
            }
            else {
                rec.operator/=(scalar);
            }
            return *this;
        }
        constexpr auto dot(versor const& rhs) const noexcept {
            if (std::is_constant_evaluated()) {
                return std::inner_product(begin(), end(), rhs.begin(), value_type());
            }
            else {
                return rec.dot(rhs.rec);
            }
        }
        constexpr auto norm() const noexcept {
            return dot(*this);
        }

    public:
        constexpr auto operator+() const noexcept {
            return *this;
        }
        constexpr auto operator-() const noexcept {
            return versor(*this).negate();
        }
        constexpr auto operator+(versor const& rhs) const noexcept {
            return versor(*this).operator+=(rhs);
        }
        constexpr auto operator-(versor const& rhs) const noexcept {
            return versor(*this).operator-=(rhs);
        }
        constexpr auto operator*(value_type scalar) const noexcept {
            return versor(*this).operator*=(scalar);
        }
        constexpr auto operator/(value_type scalar) const noexcept {
            return versor(*this).operator/=(scalar);
        }
        constexpr friend auto operator*(value_type scalar, versor v) noexcept {
            return v.operator*=(scalar);
        }
        constexpr friend auto dot(versor const& lhs, versor const& rhs) noexcept {
            return lhs.dot(rhs);
        }
        constexpr friend auto norm(versor const& v) noexcept {
            return v.norm();
        }

    public:
        template <class Ch>
        friend auto& operator<<(std::basic_ostream<Ch>& output, versor const& v) {
            return output << v.rec;
        }

    public:
        template <size_t M>
        constexpr friend auto rectangle(std::span<versor const, M> positions) noexcept {
            //static_assert(M != std::dynamic_extent);
            if (N == 0) {
                return std::tuple{versor(), versor()};
            }
            versor min = positions[0];
            versor max = positions[0];
            for (auto vertex : positions) {
                for (size_t i = 0; i < N; ++i) {
                    if (vertex[i] < min[i]) min[i] = vertex[i];
                    if (vertex[i] > max[i]) max[i] = vertex[i];
                }
            }
            return std::tuple{min, max};
        }
        template <size_t M>
        constexpr friend auto rectangular_normalize(std::span<versor const, M> positions) noexcept {
            std::array<versor, M> result{};
            if (auto [min, max] = rectangle(positions); (max-min).norm()) {
                auto translation = (min + max) / value_type(2);
                auto extent = max - min;
                auto max_extent = *std::max_element(extent.begin(), extent.end());
                for (size_t i = 0; i < std::size(positions); ++i) {
                    result[i] = (positions[i] - translation) / max_extent;
                }
            }
            return result;
        }

    public:
        array_type arr;
        reducer<N> rec;
    };

    using vec2i = versor<int,    2>;
    using vec2f = versor<float,  2>;
    using vec2d = versor<double, 2>;
    using vec3i = versor<int,    3>;
    using vec3f = versor<float,  3>;
    using vec3d = versor<double, 3>;
    using vec4i = versor<int,    4>;
    using vec4f = versor<float,  4>;
    using vec4d = versor<double, 4>;

} // ::algebra

#endif/*INCLUDE_VERSOR_HPP_2F20C605_8C99_4BB5_BF12_B0E3A0DDA6AE*/
