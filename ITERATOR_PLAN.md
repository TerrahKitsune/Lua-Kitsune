# KITSUNE_TITERATOR Implementation Plan

## Overview

Add a stateful iterator type to the Kitsune bridge so a C# `IEnumerable<LuaValue>` (or `IAsyncEnumerable<LuaValue>`) can be passed to Lua and consumed with a standard `for v in iter do` loop. The iterator appears in Lua as a plain callable closure backed by a full userdata with a `__gc` metamethod that fires `finalized` when the closure is collected.

---

## Design Decisions (agreed in review)

- **Lua representation**: a `lua_pushcclosure` (not a coroutine). The generic `for ... in` loop calls it directly.
- **`first` fires on the first Lua call**, `next` on all subsequent calls. Both dispatch through the same `LuaFunctionTrampoline`; the engine-internal `state` field decides which C# delegate to invoke.
- **`state` values in `KitsuneIteratorUD`**:
  - `0` = uncalled
  - `1` = `first` was called, now in `next` territory
  - `2` = `next` (subsequent calls)
  - `3` = finalized / dead (guard against use-after-GC)
- **`KitsuneIterator.userdata`** is host-side shared state passed to all three callbacks. The engine copies it into `KitsuneIteratorUD.iteratorUserdata` and passes it as the `userdata` argument when invoking each callback.
- **`GetEnumerator()` is called lazily** when `first` fires from Lua, not in `FillNativeVariable`. This ensures no DB connection / cursor is opened until Lua actually begins iterating.
- **`finalized` is called from `__gc`** with a no-op `resultSetter` (not `NULL`) to avoid a null-pointer crash in `LuaFunctionTrampoline`.
- **GCHandles for iterator delegates are self-cleaning**: freed inside the `finalized` callback, not added to `GlobalCFunctionHandles`.
- **`LuaIteratorRef`** is a cancellation/control handle (not a registry-anchor ref like `LuaFunctionRef`/`LuaThreadRef`). `Dispose()` signals a graceful stop; the Lua closure's next call returns `nil` and breaks the loop.
- **`IAsyncEnumerable` passed to Lua** uses `.ToBlockingEnumerable()` — blocks the scheduler thread per step. Acceptable only for fast sources. Documented as a warning.

---

## Task List

### Task 1 — C++: Define `KitsuneIteratorUD` internal struct

**File**: `KitsuneEngine.cpp` (near the top, after `KitsuneState` and before `PushKitsuneVariable`)

Add the engine-internal struct. This is `lua_newuserdata` memory — Lua owns and GC's it.

```cpp
struct KitsuneIteratorUD {
    kitsune_CFunctionData first;
    kitsune_CFunctionData next;
    kitsune_CFunctionData finalized;
    void* iteratorUserdata; // copy of KitsuneIterator.userdata; passed to each callback
    int state;              // 0=uncalled, 1=first called, 2=next, 3=finalized/dead
};
```

---

### Task 2 — C++: Register `"KitsuneIterator"` metatable in `KitsuneInit`

**File**: `KitsuneEngine.cpp`, inside `KitsuneInit`, after `luaL_openlibs(L)` and before `if (initFunc)`.

```cpp
luaL_newmetatable(L, "KitsuneIterator");
lua_pushcfunction(L, KitsuneIteratorUD_gc);
lua_setfield(L, -2, "__gc");
lua_pop(L, 1);
```

Forward-declare `KitsuneIteratorUD_gc` before `KitsuneInit`.

---

### Task 3 — C++: Implement `KitsuneIteratorUD_gc`

**File**: `KitsuneEngine.cpp` (before `KitsuneInit`)

Called by Lua GC when the userdata upvalue is collected. Sets `state = 3` before calling `finalized` so any reentrant call is a no-op. Passes a **no-op lambda** as `resultSetter` — never `nullptr` — to prevent `LuaFunctionTrampoline` from crashing.

```cpp
static int KitsuneIteratorUD_gc(lua_State* L) {
    KitsuneIteratorUD* ud = (KitsuneIteratorUD*)lua_touserdata(L, 1);
    if (!ud || ud->state == 3)
        return 0;
    ud->state = 3;
    if (ud->finalized.func) {
        auto noop = [](const KitsuneVariable*) -> int { return 1; };
        ud->finalized.func(0, nullptr, noop, ud->finalized.userdata);
    }
    return 0;
}
```

---

### Task 4 — C++: Implement `KitsuneIteratorWrapper`

**File**: `KitsuneEngine.cpp` (before `PushKitsuneVariable`)

The C closure pushed to Lua. Mirrors `LuaCFunctionWrapper` with two differences:
1. Dispatches to `first` or `next` based on `state`.
2. Returning `nil` / `KITSUNE_TNONE` is **not an error** — it signals end-of-iteration. Does **not** call `lua_error`.

```cpp
static int KitsuneIteratorWrapper(lua_State* L) {
    KitsuneIteratorUD* ud = (KitsuneIteratorUD*)lua_touserdata(L, lua_upvalueindex(1));
    if (!ud || ud->state == 3) {
        lua_pushnil(L);
        return 1;
    }

    kitsune_CFunctionData* cfd = (ud->state == 0) ? &ud->first : &ud->next;
    if (ud->state == 0)
        ud->state = 1;
    else
        ud->state = 2;

    if (!cfd->func) {
        ud->state = 3;
        lua_pushnil(L);
        return 1;
    }

    // Marshal Lua args → KitsuneVariable argv (same pattern as LuaCFunctionWrapper)
    KitsuneState* state = g_state;
    int argc = lua_gettop(L);
    KitsuneVariable* args = nullptr;
    if (argc > 0) {
        args = (KitsuneVariable*)gff_calloc(argc, sizeof(KitsuneVariable));
        if (!args) {
            lua_pushnil(L);
            return 1;
        }
    }

    lua_State* prevDelegateState = state->DelegateState;
    state->DelegateState = L;
    for (int i = 0; i < argc; i++)
        FillKitsuneVariableFromStack(L, i + 1, &args[i], false);
    lua_settop(L, 0);

    int rc = cfd->func(argc, args, LuaResultSetter, cfd->userdata);
    state->DelegateState = prevDelegateState;

    for (int i = 0; i < argc; i++)
        FreeVariableData(&args[i], L);
    gff_free(args);

    // Deferred TERROR from LuaResultSetter: raise as Lua error (same as LuaCFunctionWrapper)
    if (state->lastCallError) {
        lua_pushstring(L, state->lastCallError);
        gff_free(state->lastCallError);
        state->lastCallError = nullptr;
        lua_error(L);
        return 0; // unreachable
    }

    if (rc <= 0 || lua_gettop(L) == 0) {
        // End of iteration — push nil to break the for loop; not an error
        ud->state = 3;
        lua_pushnil(L);
        return 1;
    }

    return lua_gettop(L);
}
```

---

### Task 5 — C++: Add `case KITSUNE_TITERATOR:` to `PushKitsuneVariable`

**File**: `KitsuneEngine.cpp`, inside `PushKitsuneVariable` switch, before `default:`.

Allocates `KitsuneIteratorUD` via `lua_newuserdata` (Lua-owned), copies fields from `KitsuneIterator`, sets the `"KitsuneIterator"` metatable, then wraps it in a `lua_pushcclosure` with the userdata as upvalue 1. The closure (not the raw userdata) is what Lua receives.

```cpp
case KITSUNE_TITERATOR: {
    const KitsuneIterator* it = v->iterator;
    if (!it) {
        lua_pushnil(L);
        break;
    }
    KitsuneIteratorUD* ud = (KitsuneIteratorUD*)lua_newuserdata(L, sizeof(KitsuneIteratorUD));
    memset(ud, 0, sizeof(KitsuneIteratorUD));
    if (it->first)     ud->first     = *it->first;
    if (it->next)      ud->next      = *it->next;
    if (it->finalized) ud->finalized = *it->finalized;
    ud->iteratorUserdata = it->userdata;
    ud->state = 0;
    luaL_setmetatable(L, "KitsuneIterator");
    lua_pushcclosure(L, KitsuneIteratorWrapper, 1); // userdata is upvalue 1
    break;
}
```

---

### Task 6 — C++: Update `FreeVariableData` for `KITSUNE_TITERATOR`

**File**: `KitsuneEngine.cpp`, inside `FreeVariableData`.

The `KitsuneIterator*` is caller-owned (host allocated it, host frees it). The engine only nulls the pointer.

```cpp
else if (var->type == KITSUNE_TITERATOR) {
    var->iterator = nullptr;
}
```

---

### Task 7 — C#: Add `Iterator = -8` to `LuaType`

**File**: `KitsuneNet/LuaType.cs`

```csharp
/// <summary>Stateful iterator (KITSUNE_TITERATOR = -8).
/// Outbound (C# → Lua) only. Lua receives a callable closure; use <c>for v in iter do</c>.
/// Never returned by the engine. Create via <see cref="LuaValue.FromIterator"/>.</summary>
Iterator = -8,
```

---

### Task 8 — C#: Create `LuaIteratorRef`

**File**: `KitsuneNet/LuaIteratorRef.cs` (new file)

Cancellation/control handle returned by the `out`-parameter overloads of `FromIterator`. Unlike `LuaFunctionRef`/`LuaThreadRef`, this holds **no Lua registry reference** — it is purely a C#-side cancellation signal and convenience accessor. `Dispose()` cancels; it does not release any Lua anchor.

```csharp
public sealed class LuaIteratorRef : IDisposable
{
    private readonly IEnumerable<LuaValue>? _source;
    private readonly IAsyncEnumerable<LuaValue>? _asyncSource;
    private volatile int _cancelled;

    internal LuaIteratorRef(IEnumerable<LuaValue> source)      => _source = source;
    internal LuaIteratorRef(IAsyncEnumerable<LuaValue> source) => _asyncSource = source;

    /// <summary>True once <see cref="Cancel"/> or <see cref="Dispose"/> has been called.</summary>
    public bool IsCancelled => _cancelled != 0;

    /// <summary>Signals the iterator to stop. The next Lua call returns nil, breaking the loop.
    /// The Lua closure's GC will then fire <c>finalized</c> to release resources.</summary>
    public void Cancel() => Interlocked.Exchange(ref _cancelled, 1);

    /// <summary>Enumerates the underlying source independently of Lua.
    /// For re-enumerable sources (List, LINQ) this creates a fresh independent cursor.
    /// For single-use sources (DB cursor) only one consumer should iterate.</summary>
    public IEnumerable<LuaValue> Iterator(CancellationToken cancellationToken = default)
    {
        ObjectDisposedException.ThrowIf(_cancelled != 0, this);
        IEnumerable<LuaValue> src = _source
            ?? (_asyncSource?.ToBlockingEnumerable(cancellationToken)
                ?? throw new InvalidOperationException("No source."));
        foreach (LuaValue v in src)
        {
            cancellationToken.ThrowIfCancellationRequested();
            yield return v;
        }
    }

    /// <summary>Asynchronously enumerates the underlying source independently of Lua.
    /// Uses the <see cref="IAsyncEnumerable{T}"/> source natively when available,
    /// otherwise wraps the synchronous source.</summary>
    public async IAsyncEnumerable<LuaValue> IteratorAsync(
        [System.Runtime.CompilerServices.EnumeratorCancellation]
        CancellationToken cancellationToken = default)
    {
        ObjectDisposedException.ThrowIf(_cancelled != 0, this);
        if (_asyncSource is not null)
        {
            await foreach (LuaValue v in _asyncSource.WithCancellation(cancellationToken).ConfigureAwait(false))
                yield return v;
        }
        else if (_source is not null)
        {
            foreach (LuaValue v in _source)
            {
                cancellationToken.ThrowIfCancellationRequested();
                yield return v;
            }
        }
    }

    public void Dispose() => Cancel();
}
```

---

### Task 9 — C#: Add `IteratorValue` and `FromIterator` to `LuaValue`

**File**: `KitsuneNet/LuaValue.cs`

Add the storage property and four factory overloads (sync/async × with/without handle).

```csharp
/// <summary>Source for <see cref="LuaType.Iterator"/> values. Null for all other types.</summary>
public LuaIteratorRef? IteratorValue { get; init; }
```

```csharp
/// <summary>Creates an iterator value from a synchronous sequence.
/// When passed to the engine Lua receives a stateful closure iterable with
/// <c>for v in iter do</c>. <see cref="GetEnumerator"/> is called lazily when
/// Lua invokes the closure for the first time.</summary>
public static LuaValue FromIterator(IEnumerable<LuaValue> source)
{
    var iterRef = new LuaIteratorRef(source);
    return new() { Type = LuaType.Iterator, IteratorValue = iterRef };
}

/// <summary>Creates an iterator value and returns a control handle for cancellation
/// or independent C#-side enumeration via <see cref="LuaIteratorRef.Iterator"/>.</summary>
public static LuaValue FromIterator(IEnumerable<LuaValue> source, out LuaIteratorRef handle)
{
    handle = new LuaIteratorRef(source);
    return new() { Type = LuaType.Iterator, IteratorValue = handle };
}

/// <summary>Creates an iterator value from an async sequence. When passed to Lua
/// the async source is consumed via <c>ToBlockingEnumerable</c> — this blocks the
/// Lua scheduler thread for each step. Suitable only for fast async sources.</summary>
public static LuaValue FromIterator(IAsyncEnumerable<LuaValue> source)
{
    var iterRef = new LuaIteratorRef(source);
    return new() { Type = LuaType.Iterator, IteratorValue = iterRef };
}

/// <inheritdoc cref="FromIterator(IAsyncEnumerable{LuaValue})"/>
public static LuaValue FromIterator(IAsyncEnumerable<LuaValue> source, out LuaIteratorRef handle)
{
    handle = new LuaIteratorRef(source);
    return new() { Type = LuaType.Iterator, IteratorValue = handle };
}
```

Also add to `ToString()`:
```csharp
LuaType.Iterator => "iterator",
```

And to `Equals` / `GetHashCode`:
```csharp
ReferenceEquals(IteratorValue, other.IteratorValue)
hash.Add(IteratorValue);
```

---

### Task 10 — C#: Add `case LuaType.Iterator:` to `FillNativeVariable`

**File**: `KitsuneNet/KitsuneEngine.cs`, inside `FillNativeVariable`

This is the core marshalling step. Uses a shared `IteratorState` helper class to hold the source and lazily-created enumerator. Both `step` (shared by `first` and `next`) and `finalize` delegates close over the same `IteratorState` instance.

The `IteratorState` class (private, nested in `KitsuneEngine`):

```csharp
private sealed class IteratorState
{
    public readonly LuaIteratorRef Ref;
    public IEnumerator<LuaValue>? Enumerator; // null until first is called
    public GCHandle StepHandle;
    public GCHandle FinalizeHandle;

    public IteratorState(LuaIteratorRef iterRef) => Ref = iterRef;
}
```

The `FillNativeVariable` case:

```csharp
case LuaType.Iterator when v.IteratorValue is LuaIteratorRef iterRef:
{
    var iterState = new IteratorState(iterRef);

    LuaFunction stepFunc = _ =>
    {
        if (iterRef.IsCancelled)
            return LuaValue.None;
        // Lazy: create enumerator on first call
        iterState.Enumerator ??= iterRef.GetSyncEnumerator();
        if (iterState.Enumerator is null || !iterState.Enumerator.MoveNext())
            return LuaValue.None;
        return iterState.Enumerator.Current;
    };

    LuaFunction finalizeFunc = _ =>
    {
        iterState.Enumerator?.Dispose();
        iterState.Enumerator = null;
        if (iterState.StepHandle.IsAllocated)     iterState.StepHandle.Free();
        if (iterState.FinalizeHandle.IsAllocated) iterState.FinalizeHandle.Free();
        return LuaValue.None;
    };

    iterState.StepHandle     = GCHandle.Alloc(stepFunc);
    iterState.FinalizeHandle = GCHandle.Alloc(finalizeFunc);

    // kitsune_CFunctionData for step (first + next share the same struct)
    IntPtr stepCFD = Marshal.AllocHGlobal(IntPtr.Size * 2);
    Marshal.WriteIntPtr(stepCFD, 0,           GetTrampolinePtr());
    Marshal.WriteIntPtr(stepCFD, IntPtr.Size, GCHandle.ToIntPtr(iterState.StepHandle));
    ptrs.Add(stepCFD);

    // kitsune_CFunctionData for finalized
    IntPtr finCFD = Marshal.AllocHGlobal(IntPtr.Size * 2);
    Marshal.WriteIntPtr(finCFD, 0,           GetTrampolinePtr());
    Marshal.WriteIntPtr(finCFD, IntPtr.Size, GCHandle.ToIntPtr(iterState.FinalizeHandle));
    ptrs.Add(finCFD);

    // KitsuneIterator { first*, next*, finalized*, userdata }
    IntPtr iterStruct = Marshal.AllocHGlobal(IntPtr.Size * 4);
    Marshal.WriteIntPtr(iterStruct, 0,                stepCFD);  // first
    Marshal.WriteIntPtr(iterStruct, IntPtr.Size,      stepCFD);  // next (same)
    Marshal.WriteIntPtr(iterStruct, IntPtr.Size * 2,  finCFD);   // finalized
    Marshal.WriteIntPtr(iterStruct, IntPtr.Size * 3,  IntPtr.Zero); // userdata
    ptrs.Add(iterStruct);

    nv.Data = iterStruct;
    // GCHandles are NOT in ptrs — freed by finalizeFunc when Lua GCs the closure
    break;
}
```

`GetSyncEnumerator()` is a method on `LuaIteratorRef` that returns `IEnumerator<LuaValue>` from either the sync or async source (blocking for async):

```csharp
internal IEnumerator<LuaValue>? GetSyncEnumerator()
{
    if (_source is not null)
        return _source.GetEnumerator();
    if (_asyncSource is not null)
        return _asyncSource.ToBlockingEnumerable().GetEnumerator();
    return null;
}
```

---

### Task 11 — Tests

**File**: `KitsuneNet.Tests/KitsuneUtilTests.cs`

Add test methods covering:

- [ ] Basic iteration: `for v in Test do` receives all values in order
- [ ] Early break: `break` mid-loop; verify `finalized` fires (use a flag in the delegate)
- [ ] `Cancel()` from C#: loop stops cleanly on next step
- [ ] Empty iterator: loop body never runs
- [ ] Re-enumerable source: same `LuaIteratorRef` can be passed to Lua twice (two independent loops, each gets fresh enumerator)
- [ ] `LuaIteratorRef.Iterator()`: C#-side enumeration returns all values
- [ ] `LuaIteratorRef.IteratorAsync()`: async enumeration returns all values
- [ ] Factory pattern: `KITSUNE_TCFUNCTION` wrapping `LuaValue.FromIterator` returns fresh iterator on each Lua call

---

## File Change Summary

| File | Change |
|------|--------|
| `KitsuneEngine.cpp` | Tasks 1–6: struct, metatable, `__gc`, wrapper, `PushKitsuneVariable` case, `FreeVariableData` case |
| `KitsuneNet/LuaType.cs` | Task 7: `Iterator = -8` |
| `KitsuneNet/LuaIteratorRef.cs` | Task 8: new file |
| `KitsuneNet/LuaValue.cs` | Task 9: property + factory methods + `ToString`/`Equals`/`GetHashCode` |
| `KitsuneNet/KitsuneEngine.cs` | Task 10: `IteratorState` class + `FillNativeVariable` case + `GetSyncEnumerator` |
| `KitsuneNet.Tests/KitsuneUtilTests.cs` | Task 11: new test methods |

No changes to `KitsuneEngine.h` — the struct and constants are already defined correctly.
