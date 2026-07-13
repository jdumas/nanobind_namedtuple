#pragma once

// Public header for nanobind_namedtuple.
//
// This header provides:
//   * ``nbnt::field<&T::member>("name")`` — compile-time descriptor for a
//     single field of a C++ record.
//   * ``NB_NT_FIELD(name)`` / ``NB_NAMED_TUPLE(Type, "Name", ...)`` /
//     ``NB_NAMED_TUPLE_EX(Type, "Name", ...)`` — declaration macros that
//     emit a ``nanobind::detail::type_caster<Type>`` specialization.
//
// The caster round-trips values through plain Python ``tuple`` objects
// with an exact arity check on the input side.

#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>
#include <utility>

#include <nanobind/nanobind.h>

namespace nbnt {

// Compile-time descriptor for a single named field of a record.
//
// ``MemberPtr`` is a non-type template parameter carrying the pointer to
// the underlying C++ member. ``name`` holds the Python-visible name.
// ``default_`` is preserved for future validation by ``bind_namedtuple``
// and is ignored by the caster path.
template <auto MemberPtr> struct field {
    static constexpr auto member_ptr = MemberPtr;
    const char *name = nullptr;

    constexpr explicit field(const char *n) noexcept : name(n) {}

    template <typename V> constexpr field default_(V &&) const noexcept {
        return *this;
    }
};

namespace detail {

template <typename R, typename C> C record_of_member_ptr(R C::*);

template <auto MemberPtr> using record_of_t = decltype(record_of_member_ptr(MemberPtr));

template <typename R, typename C> R value_of_member_ptr(R C::*);

template <auto MemberPtr> using value_of_t = decltype(value_of_member_ptr(MemberPtr));

template <typename Fields, std::size_t I> using field_at = std::tuple_element_t<I, Fields>;

template <typename Fields, std::size_t I>
using field_value_t = value_of_t<field_at<Fields, I>::member_ptr>;

template <typename Type, typename Fields, typename Src, std::size_t... I>
inline bool
named_tuple_from_cpp_impl(Src &&src, ::nanobind::rv_policy policy, ::nanobind::detail::cleanup_list *cleanup, PyObject *result, std::index_sequence<I...>) noexcept {
    auto set_one = [&](auto index_c) noexcept -> bool {
        constexpr std::size_t Ix = decltype(index_c)::value;
        constexpr auto mp = field_at<Fields, Ix>::member_ptr;
        using field_t = field_value_t<Fields, Ix>;
        using caster_t = ::nanobind::detail::make_caster<field_t>;
        ::nanobind::handle child =
            caster_t::from_cpp(::nanobind::detail::forward_like_<Src>(src.*mp), policy, cleanup);
        if (!child.is_valid())
            return false;
        NB_TUPLE_SET_ITEM(result, static_cast<Py_ssize_t>(Ix), child.ptr());
        return true;
    };
    (void)set_one;
    bool ok = true;
    ((ok = ok && set_one(std::integral_constant<std::size_t, I>{})), ...);
    return ok;
}

template <typename Type, typename Fields, typename Src>
inline ::nanobind::handle named_tuple_from_cpp(
    Src &&src, ::nanobind::rv_policy policy, ::nanobind::detail::cleanup_list *cleanup
) noexcept {
    constexpr std::size_t N = std::tuple_size_v<Fields>;
    PyObject *result = PyTuple_New(static_cast<Py_ssize_t>(N));
    if (!result)
        return {};
    bool ok = named_tuple_from_cpp_impl<Type, Fields>(
        std::forward<Src>(src), policy, cleanup, result, std::make_index_sequence<N>{}
    );
    if (!ok) {
        Py_DECREF(result);
        return {};
    }
    return result;
}

template <typename Type, typename Fields, std::size_t... I>
inline bool
named_tuple_from_python_impl(::nanobind::handle src, uint8_t flags, ::nanobind::detail::cleanup_list *cleanup, Type &out, std::index_sequence<I...>) noexcept {
    auto load_one = [&](auto index_c) noexcept -> bool {
        constexpr std::size_t Ix = decltype(index_c)::value;
        constexpr auto mp = field_at<Fields, Ix>::member_ptr;
        using field_t = field_value_t<Fields, Ix>;
        using caster_t = ::nanobind::detail::make_caster<field_t>;
        caster_t caster;
        PyObject *item = PyTuple_GET_ITEM(src.ptr(), static_cast<Py_ssize_t>(Ix));
        if (!caster.from_python(
                item, ::nanobind::detail::flags_for_local_caster<field_t>(flags), cleanup
            ))
            return false;
        if (!caster.template can_cast<field_t>())
            return false;
        out.*mp = caster.operator ::nanobind::detail::cast_t<field_t>();
        return true;
    };
    (void)load_one;
    bool ok = true;
    ((ok = ok && load_one(std::integral_constant<std::size_t, I>{})), ...);
    return ok;
}

template <typename Type, typename Fields>
inline bool named_tuple_from_python(
    ::nanobind::handle src, uint8_t flags, ::nanobind::detail::cleanup_list *cleanup, Type &out
) noexcept {
    constexpr std::size_t N = std::tuple_size_v<Fields>;
    if (!src.ptr() || !PyTuple_CheckExact(src.ptr()))
        return false;
    if (PyTuple_GET_SIZE(src.ptr()) != static_cast<Py_ssize_t>(N))
        return false;
    return named_tuple_from_python_impl<Type, Fields>(
        src, flags, cleanup, out, std::make_index_sequence<N>{}
    );
}

} // namespace detail
} // namespace nbnt

// ``NB_NAMED_TUPLE_EX(Type, "ClassName", nbnt::field<&Type::a>("a"), ...)``
// emits a ``nanobind::detail::type_caster<Type>`` specialization at the
// current file scope. The macro must be invoked outside any namespace.
#define NB_NAMED_TUPLE_EX(Type, ClassName, ...)                                                    \
    NAMESPACE_BEGIN(NB_NAMESPACE)                                                                  \
    NAMESPACE_BEGIN(detail)                                                                        \
    template <> struct type_caster<Type> {                                                         \
      private:                                                                                     \
        using _nbnt_current_type [[maybe_unused]] = Type;                                          \
        using _nbnt_fields_t = decltype(::std::make_tuple(__VA_ARGS__));                           \
        static_assert(                                                                             \
            ::std::is_default_constructible_v<Type>,                                               \
            "NB_NAMED_TUPLE requires the record type to be "                                       \
            "default-constructible"                                                                \
        );                                                                                         \
                                                                                                   \
      public:                                                                                      \
        using Value = Type;                                                                        \
        template <typename T_> using Cast = ::nanobind::detail::movable_cast_t<T_>;                \
        template <typename T_> static constexpr bool can_cast() {                                  \
            return true;                                                                           \
        }                                                                                          \
        static constexpr auto Name = ::nanobind::detail::const_name(ClassName);                    \
        Value value;                                                                               \
        explicit operator Value *() {                                                              \
            return &value;                                                                         \
        }                                                                                          \
        explicit operator Value &() {                                                              \
            return value;                                                                          \
        }                                                                                          \
        explicit operator Value &&() {                                                             \
            return static_cast<Value &&>(value);                                                   \
        }                                                                                          \
        bool from_python(                                                                          \
            ::nanobind::handle src, uint8_t flags, ::nanobind::detail::cleanup_list *cleanup       \
        ) noexcept {                                                                               \
            return ::nbnt::detail::named_tuple_from_python<Type, _nbnt_fields_t>(                  \
                src, flags, cleanup, value                                                         \
            );                                                                                     \
        }                                                                                          \
        template <typename T_>                                                                     \
        static ::nanobind::handle from_cpp(                                                        \
            T_ &&v, ::nanobind::rv_policy policy, ::nanobind::detail::cleanup_list *cleanup        \
        ) noexcept {                                                                               \
            return ::nbnt::detail::named_tuple_from_cpp<Type, _nbnt_fields_t>(                     \
                ::std::forward<T_>(v), policy, cleanup                                             \
            );                                                                                     \
        }                                                                                          \
        template <                                                                                 \
            typename T_,                                                                           \
            ::nanobind::detail::enable_if_t<::std::is_same_v<::std::remove_cv_t<T_>, Type>> = 0>   \
        static ::nanobind::handle from_cpp(                                                        \
            T_ *v, ::nanobind::rv_policy policy, ::nanobind::detail::cleanup_list *cleanup         \
        ) noexcept {                                                                               \
            if (!v)                                                                                \
                return ::nanobind::none().release();                                               \
            return from_cpp(*v, policy, cleanup);                                                  \
        }                                                                                          \
    };                                                                                             \
    NAMESPACE_END(detail)                                                                          \
    NAMESPACE_END(NB_NAMESPACE)

// ``NB_NAMED_TUPLE(Type, "ClassName", NB_NT_FIELD(a), NB_NT_FIELD(b), ...)``
// is the concise form that infers the member pointer and the string name
// from each ``NB_NT_FIELD`` argument.
#define NB_NAMED_TUPLE(Type, ClassName, ...) NB_NAMED_TUPLE_EX(Type, ClassName, __VA_ARGS__)

// ``NB_NT_FIELD(member)`` expands to a ``nbnt::field<&Type::member>("member")``
// initializer. It is only valid inside the argument list of ``NB_NAMED_TUPLE``,
// where ``_nbnt_current_type`` is aliased to the enclosing record type.
#define NB_NT_FIELD(FieldName) ::nbnt::field<&_nbnt_current_type::FieldName>(#FieldName)
