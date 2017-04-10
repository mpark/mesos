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

#ifndef __PROCESS_DISPATCH_HPP__
#define __PROCESS_DISPATCH_HPP__

#include <functional>
#include <memory> // TODO(benh): Replace shared_ptr with unique_ptr.
#include <string>

#include <process/process.hpp>

#include <stout/preprocessor.hpp>
#include <stout/result_of.hpp>

namespace process {

// The dispatch mechanism enables you to "schedule" a method to get
// invoked on a process. The result of that method invocation is
// accessible via the future that is returned by the dispatch method
// (note, however, that it might not be the _same_ future as the one
// returned from the method, if the method even returns a future, see
// below). Assuming some class 'Fibonacci' has a (visible) method
// named 'compute' that takes an integer, N (and returns the Nth
// fibonacci number) you might use dispatch like so:
//
// PID<Fibonacci> pid = spawn(new Fibonacci(), true); // Use the GC.
// Future<int> f = dispatch(pid, &Fibonacci::compute, 10);
//
// Because the pid argument is "typed" we can ensure that methods are
// only invoked on processes that are actually of that type. Providing
// this mechanism for varying numbers of function types and arguments
// requires support for variadic templates, slated to be released in
// C++11. Until then, we use the Boost preprocessor macros to
// accomplish the same thing (albeit less cleanly). See below for
// those definitions.
//
// Dispatching is done via a level of indirection. The dispatch
// routine itself creates a promise that is passed as an argument to a
// partially applied 'dispatcher' function (defined below). The
// dispatcher routines get passed to the actual process via an
// internal routine called, not surprisingly, 'dispatch', defined
// below:

namespace internal {

// The internal dispatch routine schedules a function to get invoked
// within the context of the process associated with the specified pid
// (first argument), unless that process is no longer valid. Note that
// this routine does not expect anything in particular about the
// specified function (second argument). The semantics are simple: the
// function gets applied/invoked with the process as its first
// argument. Currently we wrap the function in a shared_ptr but this
// will probably change in the future to unique_ptr (or a variant).
void dispatch(
    const UPID& pid,
    const std::shared_ptr<std::function<void(ProcessBase*)>>& f,
    const Option<const std::type_info*>& functionType = None());


template <typename R>
struct Dispatch
{
  using Wrapped = typename wrap<R>::type;
  using Unwrapped = typename unwrap<R>::type;

  template <typename T, typename F>
  struct Invoke
  {
    F T::*method;
    std::shared_ptr<Promise<Unwrapped>> promise;

    template <typename... Args>
    void operator()(ProcessBase* process, Args&&... args) const
    {
      assert(process != nullptr);
      T* t = dynamic_cast<T*>(process);
      assert(t != nullptr);
      promise->set((t->*method)(std::forward<Args>(args)...));
    }
  };

  template <typename T, typename F, typename... Args>
  static Wrapped dispatch(const PID<T>& pid, F T::*method, Args&&... args)
  {
    auto promise = std::make_shared<Promise<Unwrapped>>();

    auto f = std::make_shared<std::function<void(ProcessBase*)>>(std::bind(
        Invoke<T, F>{method, promise},
        lambda::_1,
        std::forward<Args>(args)...));

    internal::dispatch(pid, f, &typeid(method));

    return promise->future();
  }

  template <typename F>
  static Wrapped dispatch(const UPID& pid, F&& f)
  {
    auto promise = std::make_shared<Promise<Unwrapped>>();

    auto f_ = std::make_shared<std::function<void(ProcessBase*)>>(
        [=](ProcessBase*) mutable { promise->set(f()); });

    internal::dispatch(pid, f_);

    return promise->future();
  }
};

template <>
struct Dispatch<void>
{
  template <typename T, typename F>
  struct Invoke
  {
    F T::*method;

    template <typename... Args>
    void operator()(ProcessBase* process, Args&&... args) const
    {
      assert(process != nullptr);
      T* t = dynamic_cast<T*>(process);
      assert(t != nullptr);
      (t->*method)(std::forward<Args>(args)...);
    }
  };

  template <typename T, typename F, typename... Args>
  static void dispatch(const PID<T>& pid, F T::*method, Args&&... args)
  {
    auto f = std::make_shared<std::function<void(ProcessBase*)>>(std::bind(
        Invoke<T, F>{method}, lambda::_1, std::forward<Args>(args)...));

    internal::dispatch(pid, f, &typeid(method));
  }

  template <typename F>
  static void dispatch(const UPID& pid, F&& f)
  {
    auto f_ = std::make_shared<std::function<void(ProcessBase*)>>(
        [=](ProcessBase*) mutable { f(); });

    internal::dispatch(pid, f_);
  }
};

template <typename R, typename T, typename F, typename... Args>
auto dispatch(const PID<T>& pid, F T::*method, Args&&... args)
  -> decltype(Dispatch<R>::dispatch(pid, method, std::forward<Args>(args)...))
{
  return Dispatch<R>::dispatch(pid, method, std::forward<Args>(args)...);
}


template <typename R, typename T, typename F, typename... Args>
auto dispatch(const Process<T>& process, F T::*method, Args&&... args)
  -> decltype(dispatch<R>(process.self(), method, std::forward<Args>(args)...))
{
  return dispatch<R>(process.self(), method, std::forward<Args>(args)...);
}


template <typename R, typename T, typename F, typename... Args>
auto dispatch(const Process<T>* process, F T::*method, Args&&... args)
  -> decltype(dispatch<R>(process->self(), method, std::forward<Args>(args)...))
{
  return dispatch<R>(process->self(), method, std::forward<Args>(args)...);
}

} // namespace internal {

#define PARAM(Z, N, P) \
  , CAT(TUPLE_ELEM(2, 0, P), N)&& CAT(TUPLE_ELEM(2, 1, P), N)

#define FORWARD(Z, N, P) \
  , std::forward<CAT(TUPLE_ELEM(2, 0, P), N)>(CAT(TUPLE_ELEM(2, 1, P), N))

#define DISPATCH(Z, N, QUALS)                                                 \
  template <                                                                  \
      typename Process,                                                       \
      typename R,                                                             \
      typename T                                                              \
      ENUM_TRAILING_PARAMS(N, typename P)                                     \
      ENUM_TRAILING_PARAMS(N, typename A)>                                    \
  auto dispatch(                                                              \
      const Process& process,                                                 \
      R (T::*method)(ENUM_PARAMS(N, P)) QUALS                                 \
      REPEAT(N, PARAM, (A, a)))                                               \
    -> decltype(                                                              \
        internal::dispatch<R>(process, method REPEAT(N, FORWARD, (A, a))))    \
  {                                                                           \
    return internal::dispatch<R>(process, method REPEAT(N, FORWARD, (A, a))); \
  }

REPEAT(12, DISPATCH, )
REPEAT(12, DISPATCH, const)
REPEAT(12, DISPATCH, &)
REPEAT(12, DISPATCH, const &)
REPEAT(12, DISPATCH, &&)
REPEAT(12, DISPATCH, const &&)

#undef DEFER
#undef FORWARD
#undef PARAM


// We use partial specialization of
//   - internal::Dispatch<void> vs
//   - internal::Dispatch<Future<R>> vs
//   - internal::Dispatch
// in order to determine whether R is void, Future or other types.
template <typename F, typename R = typename result_of<F()>::type>
auto dispatch(const UPID& pid, F&& f)
  -> decltype(internal::Dispatch<R>::dispatch(pid, std::forward<F>(f)))
{
  return internal::Dispatch<R>::dispatch(pid, std::forward<F>(f));
}

} // namespace process {

#endif // __PROCESS_DISPATCH_HPP__
