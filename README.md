# C++ primitives for asynchronous operations

### Future and Promise

Primitives for receiving asynchronous result from a task that's can be run on another thread. Differences from the `std` versions:
- `Future` has `Then()` function that sets callback that will be executed once the task is completed. Constructor of `Promise` takes executor that allows the user to control the context in which the callback will be executed.
- `Promise` can be easily embedded into a user-defined callable object by means of `EmbedPromiseIntoTask()`. The returned callable is technically copyable so it can be easily wrapped into an `std::function` that later can be moved f.ex. into a thread pool without using shared pointers. Any actual copy operations, however, will result in move and should not be done unless you know what you're doing.
- Operator `&&` allows to combine any number of `Future`s into a single `Future` that will be active untill *all* the asynchronous operations have completed. Once that happens, the callback set through `Then()` function will be executed. Callbacks set on individual futures will be executed as normal, i.e. as soon as the corresponding task has finished.
- Operator `||` allows to combine any number of `Future`s into a single `Future` that will be active untill *any* of the combined tasks has finished. At that point both the callback belonging to that particular task and the callback belonging to the combined future will be executed. The other tasks will be cancelled and their callbacks never executed.
- The callback set on a combined `Future` (obtained through any of the two operators) will be executed in *one* of the executors used by the promises belonging to the individual tasks. It is therefore important that tasks being combined are all supposed to execute their callbacks in the same context/thread.

See `async_tests.cpp` for example usage.

### Callback and Canceller

TBD
