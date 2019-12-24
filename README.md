# C++ primitives for asynchronous operations

### Future and Promise

Primitives for receiving asynchronous result from a task that's can be run on another thread. Differences from the `std` versions:
- `Future` has `Then()` function that sets callback that will be executed once the task is completed. Constructor of `Promise` takes executor that allows the user to control the context in which the callback will be executed.
- `Promise` can be easily embedded into a user-defined callable object by means of `EmbedPromiseIntoTask()`. The returned callable is technically copyable so it can be easily wrapped into an `std::function` that later can be moved f.ex. into a thread pool without using shared pointers. Any actual copy operations, however, will result in move and should not be done unless you know what you're doing.
- Operator `&&` allows to combine any number of `Future`s into a single `Future` that will be active until *all* the asynchronous operations have completed. Once that happens, the callback set through `Then()` function will be executed. Callbacks set on individual futures will be executed as normal, i.e. as soon as the corresponding task has finished.
- Operator `||` allows to combine any number of `Future`s into a single `Future` that will be active until *any* of the combined tasks has finished. At that point both the callback belonging to that particular task and the callback belonging to the combined future will be executed. The other tasks will be cancelled and their callbacks never executed.
- The callback set on a combined `Future` (obtained through any of the two operators) will be executed in *one* of the executors used by the promises belonging to the individual tasks. It is therefore important that tasks being combined are all supposed to execute their callbacks in the same context/thread.

See `async_tests.cpp` for example usage.

### Callback and Canceller

A lightweight alternative to `Future` and `Promise`. Starting an asynchronous operation requires no additional
 dynamic memory allocations, but fewer use cases are supported and there are some limitations and caveats the user
  should be aware of. Some highlights:

- `Callback` is a functor that can only be created by a `Canceller`. It can be cancelled and its state can be tracked
 using member functions of the canceller that created it.
- A `CallbackId` can be uniquely associated with a `Callback` and is used to monitor its state or cancel it.
- A `Callback` can be queried on whether it's been cancelled.
- When a `Canceller` goes out of scope all its callbacks are automatically cancelled.
- A `Canceller` has a limit on the number of simultaneously active `Callback`s it can have (default is 128).
- A `Callback` is **not** one-shot, which means it becomes inactive once it no longer exists (or is cancelled explicitly) rather than once it's been invoked.
- For fire-and-forget operations a detached and/or empty callback can be created using `Canceller::DetachedCb()` and `Canceller::NoCb()`. A callback obtained in this way can be executed after the corresponding canceller's end of life, and cannot have an associated `CallbackId`.
- `async::Schedule()` is a convenience function to move a callback into an executor.

See `src/async.cpp` for implementation details and description of internal representation.

### Synchronizers

Helpers for coordinating two or more concurrent operations that run their `Callback`s on the same thread.

- Can keep track of up to 10000 `Callback`s.
- **Not** thread-safe. All tracked callbacks must run on the same thread.
- `OnAllCompleted`: Allows to set a listener that will be called once every tracked callbacks has been executed at least
 once and the synchronizer object has gone out of scope (whichever happens last).
- `OnAnyCompleted`: Allows to set a listener that will be called once any one of the tracked callbacks has been executed
 and the synchronizer object has gone out of scope (whichever happens last).

### Memory pool

Created by specifying a sequence of fixed sizes in ascending order. When allocating, finds the most suitable size for a given object type. Relies on `std::max_align_t`. All methods in `mem::Pool` must be called from the same thread, but a `mem::PoolPtr` obtained through `Pool::Make()` can be marshalled to any other thread and die wherever it wants.
