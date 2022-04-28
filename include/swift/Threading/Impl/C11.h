//==--- C11.h - Threading abstraction implementation ----------- -*-C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2022 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//
//
// Implements threading support for C11 threads
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_THREADING_IMPL_C11_H
#define SWIFT_THREADING_IMPL_C11_H

#include <threads.h>
#include <stdatomic.h>

namespace swift {
namespace threading_impl {

#define SWIFT_C11THREADS_CHECK(expr)                                    \
do {                                                                    \
  int res_ = (expr);                                                    \
  if (res_ != thrd_success)                                             \
    swift::threading::fatal(#expr " failed with error %d\n", res_);     \
} while (0)

#define SWIFT_C11THREADS_RETURN_TRUE_OR_FALSE(expr)                     \
do {                                                                    \
  int res_ = (expr);                                                    \
  switch (res_) {                                                       \
  case thrd_success:                                                    \
    return true;                                                        \
  case thrd_busy:                                                       \
    return false;                                                       \
  default:                                                              \
    swift::threading::fatal(#expr " failed with error (%d)\n", res_);   \
  }                                                                     \
} while (0)


// .. Thread related things ..................................................

using thread_id = ::thrd_t;

inline thread_id thread_get_current() { return ::thrd_current(); }
bool thread_is_main();
inline bool threads_same(thread_id a, thread_id b) {
  return ::thrd_equal(a, b);
}

// .. Mutex support ..........................................................

using mutex_handle = ::mtx_t;

inline void mutex_init(mutex_handle &handle, bool checked=false) {
  SWIFT_C11THREADS_CHECK(::mtx_init(&handle), ::mtx_plain);
}
inline void mutex_destroy(mutex_handle &handle) {
  SWIFT_C11THREADS_CHECK(::mtx_destroy(&handle));
}

inline void mutex_lock(mutex_handle &handle) {
  SWIFT_C11THREADS_CHECK(::mtx_lock(&handle));
}
inline void mutex_unlock(mutex_handle &handle) {
  SWIFT_C11THREADS_CHECK(::mtx_unlock(&handle));
}
inline bool mutex_try_lock(mutex_handle &handle) {
  SWIFT_C11THREADS_RETURN_TRUE_OR_FALSE(::mtx_trylock(&handle));
}

inline void mutex_unsafe_lock(mutex_handle &handle) {
  (void)::mtx_lock(&handle);
}
inline void mutex_unsafe_unlock(mutex_handle &handle) {
  (void)::mtx_unlock(&handle);
}

struct lazy_mutex_handle {
  ::mtx_t      mutex;
  ::atomic_int once;    // -1 = initialized, 0 = uninitialized, 1 = initializing
};

inline constexpr lazy_mutex_handle lazy_mutex_initializer() {
  return (lazy_mutex_handle){0};
}
inline void lazy_mutex_init(lazy_mutex_handle &handle) {
  // Sadly, we can't use call_once() for this as it doesn't have a context
  if (::atomic_load_explicit(&handle.once, ::memory_order_acquire) < 0)
    return;

  if (::atomic_compare_exchange_strong_explicit(&handle.once,
                                                &(int){ 0 },
                                                1,
                                                ::memory_order_relaxed,
                                                ::memory_order_relaxed)) {
    SWIFT_C11THREADS_CHECK(::mtx_init(&handle.mutex, ::mtx_plain));
    ::atomic_store_explicit(&handle.once, -1, ::memory_order_release);
    return;
  }

  while (::atomic_load_explicit(&handle.once, memory_order_acquire) >= 0) {
    // Just spin; ::mtx_init() is very likely to be fast
  }
}

inline void lazy_mutex_destroy(lazy_mutex_handle &handle) {
  if (::atomic_load_explicit(&handle.once, ::memory_order_acquire) < 0)
    SWIFT_C11THREADS_CHECK(::mtx_destroy(&handle.mutex));
}

inline void lazy_mutex_lock(lazy_mutex_handle &handle) {
  lazy_mutex_init(handle);
  SWIFT_C11THREADS_CHECK(::mtx_lock(&handle.mutex));
}
inline void lazy_mutex_unlock(lazy_mutex_handle &handle) {
  lazy_mutex_init(handle);
  SWIFT_C11THREADS_CHECK(::mtx_unlock(&handle.mutex));
}
inline bool lazy_mutex_try_lock(lazy_mutex_handle &handle) {
  lazy_mutex_init(handle);
  SWIFT_C11THREADS_RETURN_TRUE_OR_FALSE(::mtx_trylock(&handle.mutex));
}

inline void lazy_mutex_unsafe_lock(lazy_mutex_handle &handle) {
  lazy_mutex_init(handle);
  (void)::mtx_lock(&handle.mutex);
}
inline void lazy_mutex_unsafe_unlock(lazy_mutex_handle &handle) {
  lazy_mutex_init(handle);
  (void)::mtx_unlock(&handle.mutex);
}

// .. Once ...................................................................

typedef ::atomic_int once_t;

void once_slow(once_t &predicate, void (*fn)(void *), void *context);

inline void once_impl(once_t &predicate, void (*fn)(void *), void *context) {
  // Sadly we can't use call_once() for this (no context)
  if (::atomic_load_explicit(&predicate, ::memory_order_acquire) < 0)
    return;

  once_slow(predicate, fn, context);
}

// .. Thread local storage ...................................................

#if __cplusplus >= 201103L || __has_feature(cxx_thread_local)
#define SWIFT_THREAD_LOCAL thread_local
#endif

using tls_key = ::tss_t;
using tls_dtor = void (*)(void *);

inline bool tls_alloc(tls_key &key, tls_dtor dtor) {
  return ::tss_create(&key, dtor) == thrd_success;
}

inline void *tls_get(tls_key key) {
  return ::tss_get(key);
}

inline void tls_set(tls_key key, void *ptr) {
  ::tss_set(key, ptr);
}

} // namespace threading_impl

} // namespace swift

#endif // SWIFT_THREADING_IMPL_C11_H
