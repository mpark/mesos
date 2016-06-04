// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License

#ifndef __PROCESS_DEFERRED_HPP__
#define __PROCESS_DEFERRED_HPP__

#include <functional>
#include <utility>

#include <process/dispatch.hpp>
#include <process/pid.hpp>

#include <stout/preprocessor.hpp>

namespace process {

// We need an intermeidate "deferred" type because when constructing a
// `Deferred` we won't always know the underlying function type (for
// example, if we're being passed a std::bind or a lambda). A lambda
// won't always implicitly convert to a std::function so instead we
// hold onto the functor type F and let the compiler invoke the
// necessary cast operator (below) when it actually has determined
// what type is needed. This is similar in nature to how std::bind
// works with its intermediate _Bind type (which the pre-C++11
// implementation relied on).
template <typename F>
struct _Deferred
{
public:
  template <typename... As, typename R = typename result_of<F(As...)>::type>
  auto operator()() const -> decltype(dispatch(UPID(), std::function<R()>()))
  {
    if (pid.isNone()) {
      return f();
    }
    return dispatch(pid.get(), f);
  }

#define TEMPLATE(Z, N, DATA)                                          \
  template <                                                          \
      ENUM_PARAMS(N, typename A),                                     \
      typename R = typename result_of<F(ENUM_PARAMS(N, A))>::type>    \
  auto operator()(ENUM_BINARY_PARAMS(N, A, a))                        \
    const -> decltype(dispatch(UPID(), std::function<R()>()))         \
  {                                                                   \
    if (pid.isNone()) {                                               \
      return f(ENUM_PARAMS(N, a));                                    \
    }                                                                 \
                                                                      \
    F f_ = f;                                                         \
    return dispatch(pid.get(), f);                                    \
  }

  REPEAT_FROM_TO(1, 12, TEMPLATE, _) // Args A0 -> A10.
#undef TEMPLATE

private:
  friend class Executor;

  template <typename G>
  friend _Deferred<G> defer(const UPID& pid, G g);

#define TEMPLATE(Z, N, DATA)                                            \
  template <typename T,                                                 \
            ENUM_PARAMS(N, typename P),                                 \
            ENUM_PARAMS(N, typename A)>                                 \
  friend auto defer(const PID<T>& pid,                                  \
                    void (T::*method)(ENUM_PARAMS(N, P)),               \
                    ENUM_BINARY_PARAMS(N, A, a))                        \
    -> _Deferred<decltype(std::bind(                                    \
           std::function<void(ENUM_PARAMS(N, P))>(),                    \
           ENUM_PARAMS(N, a)))>;

  REPEAT_FROM_TO(1, 12, TEMPLATE, _) // Args A0 -> A10.
#undef TEMPLATE

#define TEMPLATE(Z, N, DATA)                                            \
  template <typename R,                                                 \
            typename T,                                                 \
            ENUM_PARAMS(N, typename P),                                 \
            ENUM_PARAMS(N, typename A)>                                 \
  friend auto defer(const PID<T>& pid,                                  \
                    Future<R> (T::*method)(ENUM_PARAMS(N, P)),          \
                    ENUM_BINARY_PARAMS(N, A, a))                        \
    -> _Deferred<decltype(std::bind(                                    \
           std::function<Future<R>(ENUM_PARAMS(N, P))>(),               \
           ENUM_PARAMS(N, a)))>;

  REPEAT_FROM_TO(1, 12, TEMPLATE, _) // Args A0 -> A10.
#undef TEMPLATE

#define TEMPLATE(Z, N, DATA)                                            \
  template <typename R,                                                 \
            typename T,                                                 \
            ENUM_PARAMS(N, typename P),                                 \
            ENUM_PARAMS(N, typename A)>                                 \
  friend auto defer(const PID<T>& pid,                                  \
                    R (T::*method)(ENUM_PARAMS(N, P)),                  \
                    ENUM_BINARY_PARAMS(N, A, a))                        \
    -> _Deferred<decltype(std::bind(                                    \
           std::function<Future<R>(ENUM_PARAMS(N, P))>(),               \
           ENUM_PARAMS(N, a)))>;

  REPEAT_FROM_TO(1, 12, TEMPLATE, _) // Args A0 -> A10.
#undef TEMPLATE

  _Deferred(const UPID& pid, F f) : pid(pid), f(std::move(f)) {}
  /*implicit*/ _Deferred(F f) : f(std::move(f)) {}

  Option<UPID> pid;
  F f;
};


template <typename F>
struct Deferred : std::function<F>
{
  template <
      typename G,
      typename = decltype(std::function<F>(std::declval<_Deferred<G>>()))>
  Deferred(_Deferred<G>&& deferred) : std::function<F>(std::move(deferred)) {}

private:
  template <typename T>
  friend Deferred<void()> defer(const PID<T>& pid, void (T::*method)());

  template <typename R, typename T>
  friend Deferred<Future<R>()> defer(
      const PID<T>& pid, Future<R> (T::*method)());

  template <typename R, typename T>
  friend Deferred<Future<R>()> defer(const PID<T>& pid, R (T::*method)());

  Deferred(std::function<F> f) : std::function<F>(std::move(f)) {}
};

} // namespace process {

#endif // __PROCESS_DEFERRED_HPP__
