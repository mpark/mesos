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

#ifndef __PROCESS_DEFER_HPP__
#define __PROCESS_DEFER_HPP__

#include <functional>

#include <process/deferred.hpp>
#include <process/dispatch.hpp>
#include <process/executor.hpp>

#include <stout/preprocessor.hpp>

namespace process {

class Defer
{
private:
  template <typename T, typename F>
  struct Dispatch
  {
    PID<T> pid;
    F T::*method;

    template <typename... Args>
    auto operator()(Args&&... args) const
      -> decltype(dispatch(pid, method, std::forward<Args>(args)...))
    {
      return dispatch(pid, method, std::forward<Args>(args)...);
    }
  };

public:
  // The use of `std::bind` is necessary, as `defer` supports passing
  // placeholders as arguments that can later be filled in.
  template <typename T, typename F, typename... Args>
  static auto defer(const PID<T>& pid, F T::*method, Args&&... args)
    -> _Deferred<decltype(
        std::bind(Dispatch<T, F>{pid, method}, std::forward<Args>(args)...))>
  {
    return std::bind(Dispatch<T, F>{pid, method}, std::forward<Args>(args)...);
  }

  template <typename T, typename F, typename... Args>
  static auto defer(const Process<T>& process, F T::*method, Args&&... args)
    -> decltype(defer(process.self(), method, std::forward<Args>(args)...))
  {
    return defer(process.self(), method, std::forward<Args>(args)...);
  }

  template <typename T, typename F, typename... Args>
  static auto defer(const Process<T>* process, F T::*method, Args&&... args)
    -> decltype(defer(process->self(), method, std::forward<Args>(args)...))
  {
    return defer(process->self(), method, std::forward<Args>(args)...);
  }

  template <typename F>
  static _Deferred<F> defer(const UPID& pid, F&& f)
  {
    return _Deferred<F>(pid, std::forward<F>(f));
  }
};


#define PARAM(Z, N, P) \
  , CAT(TUPLE_ELEM(2, 0, P), N)&& CAT(TUPLE_ELEM(2, 1, P), N)

#define FORWARD(Z, N, P) \
  , std::forward<CAT(TUPLE_ELEM(2, 0, P), N)>(CAT(TUPLE_ELEM(2, 1, P), N))

#define DEFER(Z, N, QUALS)                                                \
  template <                                                              \
      typename Process,                                                   \
      typename R,                                                         \
      typename T                                                          \
      ENUM_TRAILING_PARAMS(N, typename P)                                 \
      ENUM_TRAILING_PARAMS(N, typename A)>                                \
  auto defer(                                                             \
      const Process& process,                                             \
      R (T::*method)(ENUM_PARAMS(N, P)) QUALS                             \
      REPEAT(N, PARAM, (A, a)))                                           \
    -> decltype(Defer::defer(process, method REPEAT(N, FORWARD, (A, a)))) \
  {                                                                       \
    return Defer::defer(process, method REPEAT(N, FORWARD, (A, a)));      \
  }

REPEAT(12, DEFER, )
REPEAT(12, DEFER, const)
REPEAT(12, DEFER, &)
REPEAT(12, DEFER, const &)
REPEAT(12, DEFER, &&)
REPEAT(12, DEFER, const &&)

#undef DEFER
#undef FORWARD
#undef PARAM


// The defer mechanism is very similar to the dispatch mechanism (see
// dispatch.hpp), however, rather than scheduling the method to get
// invoked, the defer mechanism returns a 'Deferred' object that when
// invoked does the underlying dispatch.

// Now we define defer calls for functors (with and without a PID):

template <typename F>
auto defer(const UPID& pid, F&& f)
  -> decltype(Defer::defer(pid, std::forward<F>(f)))
{
  return Defer::defer(pid, std::forward<F>(f));
}


template <typename F>
auto defer(F&& f) -> decltype(defer(__process__->self(), std::forward<F>(f)))
{
  if (__process__ != nullptr) {
    return defer(__process__->self(), std::forward<F>(f));
  }

  return __executor__->defer(std::forward<F>(f));
}

} // namespace process {

#endif // __PROCESS_DEFER_HPP__
