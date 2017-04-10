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

namespace process {

// We need an intermediate "deferred" type because when constructing a
// Deferred we won't always know the underlying function type (for
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
  template <typename R, typename... Args>
  operator std::function<R(Args...)>() &&
  {
    if (pid_.isNone()) {
      return std::move(f_);
    }

    UPID pid = pid_.get();
    F&& f = std::forward<F>(f_);
    return [pid, f](Args&&... args) -> R {
      return dispatch(
          pid, std::function<R()>(std::bind(f, std::forward<Args>(args)...)));
    };
  }

private:
  explicit _Deferred(const UPID& pid, F&& f)
      : pid_(pid), f_(std::forward<F>(f)) {}

  _Deferred(F&& f) : f_(std::forward<F>(f)) {}

  Option<UPID> pid_;
  F f_;

  friend class Defer;
  friend class Executor;
};


template <typename F>
struct Deferred : std::function<F>
{
  template <typename G>
  Deferred(_Deferred<G>&& deferred) : std::function<F>(std::move(deferred)) {}
};

} // namespace process {

#endif // __PROCESS_DEFERRED_HPP__
