# Module: Foundation

- **Layer:** 0 (depends on nothing else in the engine)
- **Owner role:** Foundation maintainers (see `GOVERNANCE.md`)
- **Public headers:** `include/aether/foundation/`
- **Dependencies:** C++20 standard library, OS threads (`Threads::Threads`)
- **Tests:** `tests/` → `aether_foundation_tests`
- **Benchmarks:** `benchmarks/foundation_bench.cpp`

## Thread-safety summary

| Type | Thread safe? |
| --- | --- |
| `HeapAllocator` | Yes |
| `ArenaAllocator`, `FrameAllocator`, `PoolAllocator` | No — one owner thread, or external locking |
| `JobSystem::submit`, `wait`, `parallel_for` | Yes, from any thread including inside jobs |
| `Logger`, `MemorySink`, `TraceCollector` | Yes |
| `FixedTimestep`, `Lifecycle` | No |
| `Uuid` | Value type; `generate` needs exclusive access to its RNG |

## Contracts

- Allocators return `nullptr` on exhaustion; they do not throw.
- Jobs must not throw; an escaping exception terminates the process.
- A `WaitGroup` must outlive every job submitted against it.
- `ErrorCode` numeric values are frozen; they are part of ACP.
