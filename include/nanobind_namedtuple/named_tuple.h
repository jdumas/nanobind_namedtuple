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

#include <nanobind/nanobind.h>

#if defined(NB_FREE_THREADED)
#include <atomic>
#endif

namespace nbnt {

namespace detail {

template <typename R, typename C> R value_of_member_ptr(R C::*);

template <auto MemberPtr> using value_of_t = decltype(value_of_member_ptr(MemberPtr));

} // namespace detail

// Compile-time field descriptor: stores the member pointer and name; the
// HasDefault=true specialization also holds the default_ value verbatim.
template <auto MemberPtr, bool HasDefault = false> struct field {
    using value_type = detail::value_of_t<MemberPtr>;
    static constexpr auto member_ptr = MemberPtr;
    static constexpr bool has_default_v = false;

    const char *name = nullptr;

    constexpr explicit field(const char *n) noexcept : name(n) {}

    template <typename V> field<MemberPtr, true> default_(V &&v) const {
        return field<MemberPtr, true>{name, value_type(std::forward<V>(v))};
    }
};

template <auto MemberPtr> struct field<MemberPtr, true> {
    using value_type = detail::value_of_t<MemberPtr>;
    static constexpr auto member_ptr = MemberPtr;
    static constexpr bool has_default_v = true;

    const char *name = nullptr;
    value_type default_value;
};

namespace detail {

// Per-``T`` traits, specialized by ``NB_NAMED_TUPLE_EX``.
template <typename T> struct traits;

// Per-T class slot: plain PyObject* under the GIL and with std::atomic_ref
// (publication is a CAS via atomic_ref); std::atomic<PyObject*> otherwise.
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

// Publish-exactly-once CAS into nt_class<T>::cls; returns the winner
// (``candidate`` on success, another thread's pointer on loss).
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

// Number of fields in a ``nanobind::detail::tuple``, replacing
// ``std::tuple_size_v`` so this header does not need ``<tuple>``.
template <typename Tuple> struct field_pack_size;
template <typename... Ts> struct field_pack_size<::nanobind::detail::tuple<Ts...>> {
    static constexpr std::size_t value = sizeof...(Ts);
};
template <typename Tuple>
inline constexpr std::size_t field_pack_size_v = field_pack_size<Tuple>::value;

template <typename Fields, std::size_t I> using field_at = typename Fields::template type<I>;

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
    return defaults_are_trailing_impl<Fields>(std::make_index_sequence<field_pack_size_v<Fields>>{}
    );
}

// Build the ``defaults`` tuple for collections.namedtuple. The inner
// if constexpr keeps this well-formed so bind_namedtuple's static_assert fires first.
template <typename F0, typename... Rest>
inline ::nanobind::object make_defaults_tuple_head(const F0 &f0, const Rest &...rest) {
    if constexpr (F0::has_default_v) {
        if constexpr ((Rest::has_default_v && ...)) {
            return ::nanobind::make_tuple(
                ::nanobind::cast(f0.default_value), ::nanobind::cast(rest.default_value)...
            );
        } else {
            return ::nanobind::none();
        }
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

// Fold helpers replacing std::apply: expand the Traits::fields() nanobind
// tuple via ``.template get<I>()`` under an index sequence.
template <typename Fields, std::size_t... I>
inline ::nanobind::object make_names_object(const Fields &fs, std::index_sequence<I...>) {
    return ::nanobind::make_tuple(::nanobind::str(fs.template get<I>().name)...);
}

template <typename Fields, std::size_t... I>
inline ::nanobind::object make_defaults_object(const Fields &fs, std::index_sequence<I...>) {
    return make_defaults_from(fs.template get<I>()...);
}

template <typename Fields, typename Fn, std::size_t... I>
inline void for_each_field(const Fields &fs, Fn &&fn, std::index_sequence<I...>) {
    (fn(fs.template get<I>()), ...);
}

// Fixed-size character buffer for a compile-time sanitized annotation.
template <std::size_t N> struct annot_text {
    char data[N]{};
};

// Collapse each ``@input@output@`` stubgen marker in a caster Name to its
// output side, matching nanobind's return-value signature rendering.
template <std::size_t N> constexpr annot_text<N> sanitize_annotation(const char (&text)[N]) {
    annot_text<N> out{};
    std::size_t j = 0;
    for (std::size_t i = 0; text[i] != '\0';) {
        if (text[i] == '@') {
            ++i;
            while (text[i] != '\0' && text[i] != '@')
                ++i;
            if (text[i] == '@')
                ++i;
            while (text[i] != '\0' && text[i] != '@')
                out.data[j++] = text[i++];
            if (text[i] == '@')
                ++i;
        } else {
            out.data[j++] = text[i++];
        }
    }
    return out;
}

// Python annotation string for a field: caster's sanitized Name text when
// it has no type-substitution slots, otherwise ``"typing.Any"`` for stubgen.
template <typename Field> inline const char *field_annotation_str() noexcept {
    using caster_t = ::nanobind::detail::make_caster<Field>;
    if constexpr (caster_t::Name.type_count() == 0) {
        static constexpr auto sanitized = sanitize_annotation(caster_t::Name.text);
        return sanitized.data;
    } else {
        return "typing.Any";
    }
}

// True when an annotation mentions ``typing.``; such strings only evaluate
// under ``typing.get_type_hints()`` if the owning module can resolve ``typing``.
constexpr bool annotation_references_typing(const char *s) noexcept {
    for (; *s != '\0'; ++s) {
        const char *p = s;
        const char *q = "typing.";
        while (*q != '\0' && *p == *q) {
            ++p;
            ++q;
        }
        if (*q == '\0')
            return true;
    }
    return false;
}

// Child rv_policy: reference/automatic_reference downgrade to copy (move
// for rvalue parents); copy/move/automatic/none pass through unchanged.
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

// True for reference_internal/take_ownership, which cannot target a
// tuple-shaped instance; callers must raise TypeError before allocating.
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
    constexpr std::size_t N = field_pack_size_v<Fields>;
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

// Strict from_python: exact Py_TYPE match to T's registered class or an
// exact-arity plain tuple; any other mismatch returns false without raising.
template <typename Type, typename Fields>
inline bool named_tuple_from_python(
    ::nanobind::handle src, uint8_t flags, ::nanobind::detail::cleanup_list *cleanup, Type &out
) noexcept {
    constexpr std::size_t N = field_pack_size_v<Fields>;
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
    if constexpr (N == 0) {
        (void)flags;
        (void)cleanup;
        (void)out;
        return true;
    } else {
        return named_tuple_from_python_impl<Type, Fields>(
            src, flags, cleanup, out, std::make_index_sequence<N>{}
        );
    }
}

// Cached collections.namedtuple factory; the reference is intentionally
// leaked for the process lifetime so no static dtor runs after Py_Finalize.
inline PyObject *nt_factory_cached() {
    static PyObject *factory = []() {
        ::nanobind::object f = ::nanobind::module_::import_("collections").attr("namedtuple");
        return f.release().ptr();
    }();
    return factory;
}

// Registration tail: factory + sentinels set pre-CAS (fully populated when
// observed); ``__annotations__`` is a copy protecting ``__nb_nt_annotations__``. Strong ref.
NB_NOINLINE inline PyObject *nt_finalize_class(
    PyObject *m_ptr, const char *class_name, PyObject *names, PyObject *defaults,
    PyObject *annotations
) {
    ::nanobind::object cls_obj = ::nanobind::borrow(nt_factory_cached())(
        ::nanobind::str(class_name), ::nanobind::borrow(names),
        ::nanobind::arg("defaults") = ::nanobind::borrow(defaults),
        ::nanobind::arg("module") = ::nanobind::borrow(m_ptr).attr("__name__")
    );

    PyObject *annotations_copy = PyDict_Copy(annotations);
    if (annotations_copy == nullptr)
        throw ::nanobind::python_error();
    ::nanobind::dict annotations_public = ::nanobind::steal<::nanobind::dict>(annotations_copy);
    if (PyObject_SetAttrString(cls_obj.ptr(), "__nb_named_tuple__", Py_True) < 0)
        throw ::nanobind::python_error();
    if (PyObject_SetAttrString(cls_obj.ptr(), "__nb_nt_annotations__", annotations) < 0)
        throw ::nanobind::python_error();
    if (PyObject_SetAttrString(cls_obj.ptr(), "__annotations__", annotations_public.ptr()) < 0)
        throw ::nanobind::python_error();

    return cls_obj.release().ptr();
}

// Attach the published class to ``m`` under ``class_name``; factored out
// to keep the per-``T`` template body slim.
NB_NOINLINE inline void
nt_attach_to_module(PyObject *m_ptr, const char *class_name, PyObject *cls) {
    if (PyObject_SetAttrString(m_ptr, class_name, cls) < 0)
        throw ::nanobind::python_error();
}

} // namespace detail

// Register the Python namedtuple class for ``T`` and attach it to ``m``.
// Publish-exactly-once (first-registration wins) process-wide.
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
        detail::nt_attach_to_module(m.ptr(), Traits::class_name, existing);
        return;
    }

    // Assemble names, trailing-defaults, and per-field annotation strings
    // from compile-time metadata; slotted casters fall back to typing.Any.
    auto fields = Traits::fields();
    constexpr std::size_t N = detail::field_pack_size_v<Fields>;
    ::nanobind::object names = detail::make_names_object(fields, std::make_index_sequence<N>{});
    ::nanobind::object defaults =
        detail::make_defaults_object(fields, std::make_index_sequence<N>{});
    ::nanobind::dict annotations;
    bool needs_typing = false;
    auto add_annot = [&](const auto &f) {
        using field_value_type = typename std::remove_reference_t<decltype(f)>::value_type;
        const char *annot = detail::field_annotation_str<field_value_type>();
        needs_typing = needs_typing || detail::annotation_references_typing(annot);
        annotations[::nanobind::str(f.name)] = ::nanobind::str(annot);
    };
    detail::for_each_field(fields, add_annot, std::make_index_sequence<N>{});

    // Make ``typing.``-prefixed annotations resolvable for get_type_hints(),
    // mirroring the ``import typing`` a hand-written module would carry.
    if (needs_typing && !::nanobind::hasattr(m, "typing"))
        m.attr("typing") = ::nanobind::module_::import_("typing");

    // Delegate the type-independent tail to a non-template helper so each
    // bind_namedtuple<T> only carries the per-T publish-once CAS below.
    PyObject *candidate = detail::nt_finalize_class(
        m.ptr(), Traits::class_name, names.ptr(), defaults.ptr(), annotations.ptr()
    );

    PyObject *winner = detail::publish_nt_cls<T>(candidate);
    if (winner != candidate) {
        Py_DECREF(candidate);
        candidate = winner;
    }

    detail::nt_attach_to_module(m.ptr(), Traits::class_name, candidate);
}

} // namespace nbnt

// NB_NAMED_TUPLE_EX emits the traits<Type> and type_caster<Type>
// specializations at file scope; must be invoked outside any namespace.
#define NB_NAMED_TUPLE_EX(Type, ClassName, ...)                                                    \
    namespace nbnt {                                                                               \
    namespace detail {                                                                             \
    template <> struct traits<Type> {                                                              \
        using _nbnt_current_type [[maybe_unused]] = Type;                                          \
        static constexpr const char *class_name = ClassName;                                       \
        using fields_t = decltype(::nanobind::detail::tuple(__VA_ARGS__));                         \
        static fields_t fields() {                                                                 \
            return ::nanobind::detail::tuple(__VA_ARGS__);                                         \
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
        NB_TYPE_CASTER(Type, ::nanobind::detail::const_name(ClassName))                            \
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
    };                                                                                             \
    NAMESPACE_END(detail)                                                                          \
    NAMESPACE_END(NB_NAMESPACE)

// NB_NAMED_TUPLE is the concise form: infers the member pointer and name
// from each NB_NT_FIELD argument.
#define NB_NAMED_TUPLE(Type, ClassName, ...) NB_NAMED_TUPLE_EX(Type, ClassName, __VA_ARGS__)

// NB_NT_FIELD(member) expands to nbnt::field<&Type::member>("member"); valid
// only inside NB_NAMED_TUPLE, where _nbnt_current_type aliases the record.
#define NB_NT_FIELD(FieldName) ::nbnt::field<&_nbnt_current_type::FieldName>(#FieldName)
