using System;
using System.Collections.Generic;
using System.Text;

namespace KitsuneNet
{
    /// <summary>Status of a coroutine managed by the engine.</summary>
    public enum CoroutineStatus
    {
        /// <summary>ID not found — never existed, already released, or fully compacted.</summary>
        None = 0,

        /// <summary>Alive and queued; waiting to be resumed by the scheduler.</summary>
        Idle = 1,

        /// <summary>Alive but suspended for a <c>Sleep()</c> deadline.</summary>
        Sleeping = 2,

        /// <summary>Currently executing inside <c>lua_resume</c>.</summary>
        Running = 3,

        /// <summary>Finished successfully; result not yet consumed.</summary>
        Done = 4,

        /// <summary>Finished with a runtime or Lua error. Call <see cref="KitsuneEngine.GetError"/> to read the message.</summary>
        Faulted = 5,

        /// <summary>Stopped by an explicit <see cref="KitsuneEngine.Cancel"/> call, or cancel is pending
        /// but the scheduler has not yet processed it — callers can treat both the same way.</summary>
        Cancelled = 6,

        /// <summary>An inline sync call (<c>RunString</c>, <c>RunFunction</c>, etc.) temporarily
        /// paused in a cooperative yield window (<c>Yield()</c> or <c>Sleep()</c>).
        /// The calling thread will resume it imminently. Not queued for the scheduler.</summary>
        Inline = 7,

        /// <summary>Suspended inside the coroutine via <c>Pause()</c>; waiting for
        /// <see cref="KitsuneEngine.Resume(int)"/> or <c>task:Resume(value)</c>.
        /// A value can be delivered and will be returned by <c>Pause()</c>.</summary>
        Paused = 8,

        /// <summary>Suspended inside the coroutine via <c>task:Wait()</c>.
        /// Can be force-woken early via <see cref="KitsuneEngine.Resume(int)"/> — no value is delivered.</summary>
        Waiting = 9,
    }
}
