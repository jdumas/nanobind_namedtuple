#pragma once

// Public header for nanobind_namedtuple.
//
// This header provides:
//   * ``nbnt::field<&T::member>("name")`` — compile-time descriptor for a
//     single field of a C++ record, with an optional ``.default_(value)``.
//   * ``NB_NT_FIELD(name)`` / ``NB_NAMED_TUPLE(Type, "Name", ...)`` /
//     ``NB_NAMED_TUPLE_EX(Type, "Name", ...)`` — declaration macros that
//     emit both a ``nbnt::detail::traits<Type>`` specialization and a
//     ``nanobind::detail::type_caster<Type>`` specialization.
//   * ``nbnt::bind_namedtuple<T>(m)`` — module-side call that builds the
//     Python namedtuple class via ``collections.namedtuple()`` and
//     publishes it to a per-``T`` slot with publish-exactly-once
//     semantics (first-registration wins).

#include <cstddef>
#include <cstdint>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

#include <nanobind/nanobind.h>

#if defined(NB_FREE_THREADED)
#include <atomic>
#endif

namespace nbnt {

namespace detail {

template <typename R, typename C> C record_of_member_ptr(R C::*);

template <auto MemberPtr> using record_of_t = decltype(record_of_member_ptr(MemberPtr));

template <typename R, typename C> R value_of_member_ptr(R C::*);

template <auto MemberPtr> using value_of_t = decltype(value_of_member_ptr(MemberPtr));

} // namespace detail

// Compile-time descriptor for a single named field of a record.
//
// ``MemberPtr`` is a non-type template parameter carrying the pointer to
// the underlying C++ member. ``HasDefault`` encodes whether a default value
// was supplied via ``.default_(...)``. ``name`` holds the Python-visible
// name and ``default_value`` (present only when ``HasDefault``) holds the
// value forwarded to ``collections.namedtuple``'s ``defaults=`` argument.
template <auto MemberPtr, bool HasDefault = false> struct field {
    using value_type = detail::value_of_t<MemberPtr>;
    static constexpr auto member_ptr = MemberPtr;
    static constexpr bool has_default_v = HasDefault;

    const char *name = nullptr;
    std::optional<value_type> default_value;

    constexpr explicit field(const char *n) noexcept : name(n) {}
    constexpr field(
        const char *n, std::optional<value_type> d
    ) noexcept(std::is_nothrow_move_constructible_v<std::optional<value_type>>)
        : name(n), default_value(std::move(d)) {}

    template <typename V> field<MemberPtr, true> default_(V &&v) const {
        return field<MemberPtr, true>(
            name, std::optional<value_type>(std::in_place, std::forward<V>(v))
        );
    }
};

namespace detail {

// Per-``T`` traits, specialized by ``NB_NAMED_TUPLE_EX``.
template <typename T> struct traits;

// Per-``T`` published class slot. Storage type varies by build:
//   * GIL builds and free-threaded builds with ``std::atomic_ref``: plain
//     ``PyObject *``. Under the GIL the publication is a plain
//     check-and-store; on free-threaded builds with ``std::atomic_ref`` the
//     publication is a create-once CAS through the atomic_ref, but the
//     storage stays plain so the hot-path relaxed load compiles to a plain
//     load.
//   * Free-threaded builds without ``std::atomic_ref``: promoted to
//     ``std::atomic<PyObject *>``.
#if defined(NB_FREE_THREADED) && !(defined(__cpp_lib_atomic_ref) && __cpp_lib_atomic_ref >= 201806L)
template <typename T> struct nt_class {
    inline static std::atomic<PyObject *> cls{nullptr};
    static_assert(
        std::atomic<PyObject *>::is_always_lock_free,
        "std::atomic<PyObject *> must be lock-free on all supported platforms"
    );
};
#else
template <typename T> struct nt_class {
    inline static PyObject *cls{nullptr};
#if defined(NB_FREE_THREADED)
    static_assert(
        std::atomic_ref<PyObject *>::is_always_lock_free,
        "std::atomic_ref<PyObject *> must be lock-free on all supported platforms"
    );
#endif
};
#endif

// Hot-path load: relaxed on FT, plain read under the GIL.
template <typename T> inline PyObject *load_nt_cls_hot() noexcept {
#if defined(NB_FREE_THREADED)
#if defined(__cpp_lib_atomic_ref) && __cpp_lib_atomic_ref >= 201806L
    return std::atomic_ref<PyObject *>(nt_class<T>::cls).load(std::memory_order_relaxed);
#else
    return nt_class<T>::cls.load(std::memory_order_relaxed);
#endif
#else
    return nt_class<T>::cls;
#endif
}

// Cold pre-check load in the publication path: acquire on FT, plain under
// the GIL. Pairs with the winner's release-store on CAS success.
template <typename T> inline PyObject *load_nt_cls_cold() noexcept {
#if defined(NB_FREE_THREADED)
#if defined(__cpp_lib_atomic_ref) && __cpp_lib_atomic_ref >= 201806L
    return std::atomic_ref<PyObject *>(nt_class<T>::cls).load(std::memory_order_acquire);
#else
    return nt_class<T>::cls.load(std::memory_order_acquire);
#endif
#else
    return nt_class<T>::cls;
#endif
}

// Publish ``candidate`` into ``nt_class<T>::cls`` with publish-exactly-once
// semantics. Returns the winning pointer: on success, ``candidate`` itself;
// on failure, the pointer another thread published first.
template <typename T> inline PyObject *publish_nt_cls(PyObject *candidate) noexcept {
#if defined(NB_FREE_THREADED)
    PyObject *expected = nullptr;
#if defined(__cpp_lib_atomic_ref) && __cpp_lib_atomic_ref >= 201806L
    std::atomic_ref<PyObject *> ref(nt_class<T>::cls);
    if (ref.compare_exchange_strong(
            expected, candidate, std::memory_order_release, std::memory_order_acquire
        ))
        return candidate;
    return expected;
#else
    if (nt_class<T>::cls.compare_exchange_strong(
            expected, candidate, std::memory_order_release, std::memory_order_acquire
        ))
        return candidate;
    return expected;
#endif
#else
    if (nt_class<T>::cls == nullptr) {
        nt_class<T>::cls = candidate;
        return candidate;
    }
    return nt_class<T>::cls;
#endif
}

template <typename Fields, std::size_t I> using field_at = std::tuple_element_t<I, Fields>;

template <typename Fields, std::size_t I>
using field_value_t = value_of_t<field_at<Fields, I>::member_ptr>;

template <typename Fields, std::size_t... I>
constexpr bool defaults_are_trailing_impl(std::index_sequence<I...>) {
    bool seen_default = false;
    bool ok = true;
    auto check_one = [&](bool has_default) {
        if (has_default) {
            seen_default = true;
        } else if (seen_default) {
            ok = false;
        }
    };
    (check_one(field_at<Fields, I>::has_default_v), ...);
    return ok;
}

template <typename Fields> constexpr bool defaults_are_trailing() {
    return defaults_are_trailing_impl<Fields>(std::make_index_sequence<std::tuple_size_v<Fields>>{}
    );
}

template <typename Fields, std::size_t... I>
constexpr std::size_t count_defaults_impl(std::index_sequence<I...>) {
    return (0 + ... + (field_at<Fields, I>::has_default_v ? 1u : 0u));
}

template <typename Fields> constexpr std::size_t count_defaults() {
    return count_defaults_impl<Fields>(std::make_index_sequence<std::tuple_size_v<Fields>>{});
}

// Build the ``defaults`` tuple to hand to ``collections.namedtuple``.
// The trailing-suffix invariant is enforced by a static_assert in
// ``bind_namedtuple<T>``, so once we hit the first defaulted field every
// remaining field is also defaulted and ``.value()`` is safe.
inline ::nanobind::object make_defaults_tuple() {
    return ::nanobind::none();
}

template <typename F0, typename... Rest>
inline ::nanobind::object make_defaults_tuple_head(const F0 &f0, const Rest &...rest) {
    if constexpr (F0::has_default_v) {
        return ::nanobind::make_tuple(
            ::nanobind::cast(f0.default_value.value()),
            ::nanobind::cast(rest.default_value.value())...
        );
    } else if constexpr (sizeof...(Rest) == 0) {
        return ::nanobind::none();
    } else {
        return make_defaults_tuple_head(rest...);
    }
}

template <typename... Fs> inline ::nanobind::object make_defaults_from(const Fs &...fs) {
    if constexpr (sizeof...(Fs) == 0) {
        return ::nanobind::none();
    } else {
        return make_defaults_tuple_head(fs...);
    }
}

// Compile-time-derived Python annotation string for a single field type.
// Returns the caster's ``Name`` text when it has no ``%`` type-substitution
// slots (which covers Python built-in scalars, ``nb::object``, and nested
// ``NB_NAMED_TUPLE``-bound classes); returns ``"typing.Any"`` when the
// caster has substitution slots (parameterised STL container casters, etc.)
// so the stubgen hook can still produce a syntactically-valid annotation.
template <typename Field> inline const char *field_annotation_str() noexcept {
    using caster_t = ::nanobind::detail::make_caster<Field>;
    if constexpr (caster_t::Name.type_count() == 0) {
        return caster_t::Name.text;
    } else {
        return "typing.Any";
    }
}

// Compute the rv_policy handed down to child field casters.
//
// The parent value handed to ``named_tuple_from_cpp`` is always a temporary
// (a value or rvalue reference), or at best a caller-owned lvalue whose
// lifetime is unrelated to the freshly-minted tuple. Field references into
// its members are therefore never safe to publish to Python.
//
// The cascade rule:
//   * ``reference`` / ``automatic_reference`` are downgraded to ``copy``
//     for lvalue parents and ``move`` for rvalue parents, so nested field
//     casters materialise their own copies instead of retaining a pointer
//     into a soon-to-die parent.
//   * ``reference_internal``, ``take_ownership`` are rejected at the caster
//     boundary: neither composes with a tuple-shaped instance (tuples lack
//     ``__weakref__`` for lifetime tracking, and this caster has no C++
//     instance to hand ownership over). Callers see a clean ``TypeError``.
//   * ``copy``, ``move``, ``automatic``, ``none`` pass through unchanged.
template <typename Src>
inline ::nanobind::rv_policy child_policy_from(::nanobind::rv_policy policy) noexcept {
    constexpr bool src_is_rvalue = !std::is_lvalue_reference_v<Src>;
    switch (policy) {
    case ::nanobind::rv_policy::reference:
    case ::nanobind::rv_policy::automatic_reference:
        return src_is_rvalue ? ::nanobind::rv_policy::move : ::nanobind::rv_policy::copy;
    default:
        return policy;
    }
}

// Return ``true`` when ``policy`` is one of the ownership / lifetime-tracking
// policies that cannot legally target a tuple-shaped instance produced by
// this caster. Callers must raise a ``TypeError`` and return an invalid
// handle before allocating the output tuple.
inline bool is_rejected_parent_policy(::nanobind::rv_policy policy) noexcept {
    return policy == ::nanobind::rv_policy::reference_internal ||
           policy == ::nanobind::rv_policy::take_ownership;
}

template <typename Type, typename Fields, typename Src, std::size_t... I>
inline bool
named_tuple_fill_impl(Src &&src, ::nanobind::rv_policy policy, ::nanobind::detail::cleanup_list *cleanup, PyObject *result, std::index_sequence<I...>) noexcept {
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
    PyObject *cls = load_nt_cls_hot<Type>();
    if (!cls) {
        PyErr_Format(
            PyExc_RuntimeError,
            "nanobind_namedtuple: type '%s' has no registered class -- "
            "call nbnt::bind_namedtuple<T>(m) in NB_MODULE",
            traits<Type>::class_name
        );
        return {};
    }
    if (is_rejected_parent_policy(policy)) {
        PyErr_Format(
            PyExc_TypeError,
            "nanobind_namedtuple: type '%s' cannot be returned with rv_policy "
            "'reference_internal' or 'take_ownership' -- tuple-shaped "
            "instances do not support lifetime tracking or ownership transfer",
            traits<Type>::class_name
        );
        return {};
    }
    ::nanobind::rv_policy child_policy = child_policy_from<Src>(policy);
    if constexpr (N == 0) {
        (void)src;
        (void)policy;
        (void)cleanup;
        (void)child_policy;
        return PyObject_CallObject(cls, nullptr);
    } else {
        PyObject *result = PyTuple_New(static_cast<Py_ssize_t>(N));
        if (!result)
            return {};
        bool ok = named_tuple_fill_impl<Type, Fields>(
            std::forward<Src>(src), child_policy, cleanup, result, std::make_index_sequence<N>{}
        );
        if (!ok) {
            Py_DECREF(result);
            return {};
        }
        Py_INCREF(cls);
        Py_SET_TYPE(result, reinterpret_cast<PyTypeObject *>(cls));
        return result;
    }
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

// Strict ``from_python`` acceptance:
//   * an instance of the registered class for ``T`` (exact ``Py_TYPE``
//     identity, never ``PyObject_IsInstance`` — subclasses are rejected);
//   * an exact-arity plain ``tuple`` (``PyTuple_CheckExact`` — non-exact
//     ``tuple`` subclasses, including other namedtuples, are rejected).
// If ``T`` has no registered class yet, the exact-identity branch is
// skipped and the plain-tuple branch remains available. On any mismatch or
// per-field conversion failure the function returns ``false`` cleanly so
// nanobind's overload resolution can move on; it never raises.
template <typename Type, typename Fields>
inline bool named_tuple_from_python(
    ::nanobind::handle src, uint8_t flags, ::nanobind::detail::cleanup_list *cleanup, Type &out
) noexcept {
    constexpr std::size_t N = std::tuple_size_v<Fields>;
    PyObject *src_ptr = src.ptr();
    if (!src_ptr)
        return false;
    PyObject *cls = load_nt_cls_hot<Type>();
    bool is_registered =
        (cls != nullptr) && (Py_TYPE(src_ptr) == reinterpret_cast<PyTypeObject *>(cls));
    bool is_plain_tuple = PyTuple_CheckExact(src_ptr);
    if (!is_registered && !is_plain_tuple)
        return false;
    if (PyTuple_GET_SIZE(src_ptr) != static_cast<Py_ssize_t>(N))
        return false;
    return named_tuple_from_python_impl<Type, Fields>(
        src, flags, cleanup, out, std::make_index_sequence<N>{}
    );
}

} // namespace detail

// Register the Python namedtuple class for ``T`` and attach it to module
// ``m`` under the macro-supplied class name. Publish-exactly-once
// (first-registration wins) process-wide.
template <typename T> inline void bind_namedtuple(::nanobind::module_ m) {
    using Traits = detail::traits<T>;
    using Fields = typename Traits::fields_t;
    static_assert(
        detail::defaults_are_trailing<Fields>(),
        "nbnt::bind_namedtuple: fields with .default_(...) must form a contiguous "
        "trailing suffix of the field list"
    );

    // Fast idempotent-reattach path: another module already published a
    // class for ``T``; adopt it and attach as a module attribute.
    if (PyObject *existing = detail::load_nt_cls_cold<T>()) {
        if (PyObject_SetAttrString(m.ptr(), Traits::class_name, existing) < 0)
            throw ::nanobind::python_error();
        return;
    }

    // Assemble the field-names tuple and the trailing-defaults tuple from
    // the compile-time metadata.
    auto fields = Traits::fields();
    ::nanobind::object names = std::apply(
        [](const auto &...fs) { return ::nanobind::make_tuple(::nanobind::str(fs.name)...); },
        fields
    );
    ::nanobind::object defaults =
        std::apply([](const auto &...fs) { return detail::make_defaults_from(fs...); }, fields);

    // Cache the ``collections.namedtuple`` factory in a function-local
    // static holding a raw ``PyObject *``. The reference is intentionally
    // leaked for the process lifetime so no C++ static destructor touches
    // Python state after ``Py_Finalize``.
    static PyObject *nt_factory = []() {
        ::nanobind::object f = ::nanobind::module_::import_("collections").attr("namedtuple");
        return f.release().ptr();
    }();

    ::nanobind::object cls_obj = ::nanobind::borrow(nt_factory)(
        ::nanobind::str(Traits::class_name), names, ::nanobind::arg("defaults") = defaults,
        ::nanobind::arg("module") = m.attr("__name__")
    );

    // Attach the stubgen sentinel and per-field annotation metadata to the
    // freshly-minted class before the CAS. Annotation strings are derived
    // from each field caster's compile-time ``Name`` descr; casters with
    // type-substitution slots fall back to ``"typing.Any"``. Setting the
    // attributes here (rather than post-CAS) keeps the class fully
    // populated before any other thread can observe it through the
    // publication's release-store.
    ::nanobind::dict annotations;
    auto add_annot = [&](const auto &f) {
        using field_value_type = typename std::remove_reference_t<decltype(f)>::value_type;
        annotations[::nanobind::str(f.name)] =
            ::nanobind::str(detail::field_annotation_str<field_value_type>());
    };
    std::apply([&](const auto &...fs) { (add_annot(fs), ...); }, fields);
    if (PyObject_SetAttrString(cls_obj.ptr(), "__nb_named_tuple__", Py_True) < 0)
        throw ::nanobind::python_error();
    if (PyObject_SetAttrString(cls_obj.ptr(), "__nb_nt_annotations__", annotations.ptr()) < 0)
        throw ::nanobind::python_error();
    if (PyObject_SetAttrString(cls_obj.ptr(), "__annotations__", annotations.ptr()) < 0)
        throw ::nanobind::python_error();

    // Release into a raw PyObject* that we intentionally leak for the
    // process lifetime on the winning branch; the losing branch drops it.
    PyObject *candidate = cls_obj.release().ptr();

    PyObject *winner = detail::publish_nt_cls<T>(candidate);
    if (winner != candidate) {
        Py_DECREF(candidate);
        candidate = winner;
    }

    if (PyObject_SetAttrString(m.ptr(), Traits::class_name, candidate) < 0)
        throw ::nanobind::python_error();
}

} // namespace nbnt

// ``NB_NAMED_TUPLE_EX(Type, "ClassName", nbnt::field<&Type::a>("a"), ...)``
// emits both a ``nbnt::detail::traits<Type>`` specialization (used by
// ``bind_namedtuple<T>`` to reach the compile-time field metadata) and a
// ``nanobind::detail::type_caster<Type>`` specialization at the current
// file scope. The macro must be invoked outside any namespace.
#define NB_NAMED_TUPLE_EX(Type, ClassName, ...)                                                    \
    namespace nbnt {                                                                               \
    namespace detail {                                                                             \
    template <> struct traits<Type> {                                                              \
        using _nbnt_current_type [[maybe_unused]] = Type;                                          \
        static constexpr const char *class_name = ClassName;                                       \
        using fields_t = decltype(::std::make_tuple(__VA_ARGS__));                                 \
        static fields_t fields() {                                                                 \
            using _nbnt_current_type [[maybe_unused]] = Type;                                      \
            return ::std::make_tuple(__VA_ARGS__);                                                 \
        }                                                                                          \
    };                                                                                             \
    }                                                                                              \
    }                                                                                              \
    NAMESPACE_BEGIN(NB_NAMESPACE)                                                                  \
    NAMESPACE_BEGIN(detail)                                                                        \
    template <> struct type_caster<Type> {                                                         \
      private:                                                                                     \
        using _nbnt_fields_t = typename ::nbnt::detail::traits<Type>::fields_t;                    \
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
