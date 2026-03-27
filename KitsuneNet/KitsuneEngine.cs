using System.Runtime.InteropServices;
using System.Text;

namespace KitsuneNet
{
    public sealed class KitsuneEngine : IDisposable
    {
        private const string DllName = "KitsuneEngine";

        private bool _disposed;

        #region P/Invoke

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr KitsuneInit();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        private static extern int KitsuneExecuteFile(string path, int argc, string[]? argv);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        private static extern int KitsuneExecuteString(string script, int argc, string[]? argv);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr KitsuneGetError();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern int KitsuneIsRunning();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern void KitsuneInterrupt();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool KitsuneSetVariable(string name, byte[] value, nuint length);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        private static extern nuint KitsuneGetVariable(string name, byte[] buffer, nuint bufferSize);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern nuint KitsuneHasResult();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern nuint KitsuneGetResult(byte[] buffer, nuint bufferSize);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern void KitsuneCleanup();

        #endregion

        /// <summary>
        /// Initialises the engine. Throws if <c>KitsuneInit</c> returns NULL.
        /// </summary>
        public KitsuneEngine()
        {
            if (KitsuneInit() == IntPtr.Zero)
                throw new InvalidOperationException(GetError() ?? "KitsuneInit failed");
        }

        /// <summary>Executes a Lua script file and returns the exit code.</summary>
        public int ExecuteFile(string path, params string[]? args)
        {
            string[] argv = args ?? [];
            return KitsuneExecuteFile(path, argv.Length, argv);
        }

        /// <summary>Executes a Lua script string and returns the exit code.</summary>
        public int ExecuteString(string script, params string[]? args)
        {
            string[] argv = args ?? [];
            return KitsuneExecuteString(script, argv.Length, argv);
        }

        /// <summary>Returns the last error message, or <c>null</c> if none.</summary>
        public string? GetError()
        {
            IntPtr ptr = KitsuneGetError();
            return ptr == IntPtr.Zero ? null : Marshal.PtrToStringAnsi(ptr);
        }

        /// <summary>Returns <c>true</c> if the engine is currently executing a script.</summary>
        public bool IsRunning => KitsuneIsRunning() != 0;

        /// <summary>Signals the running script to stop at the next instruction boundary.</summary>
        public void Interrupt() => KitsuneInterrupt();

        /// <summary>Blocks until the engine is no longer running, or the token is cancelled.</summary>
        public void Wait(CancellationToken cancellationToken = default)
        {
            while (IsRunning)
            {
                cancellationToken.ThrowIfCancellationRequested();
                Thread.Sleep(1);
            }
        }

        /// <summary>Sets a pending global variable from a UTF-8 string.</summary>
        public bool SetVariable(string name, string value)
        {
            byte[] bytes = Encoding.UTF8.GetBytes(value);
            return KitsuneSetVariable(name, bytes, (nuint)bytes.Length);
        }

        /// <summary>Sets a pending global variable from raw bytes.</summary>
        public bool SetVariable(string name, byte[] value) =>
            KitsuneSetVariable(name, value, (nuint)value.Length);

        /// <summary>
        /// Returns a global variable as a UTF-8 string, or <c>null</c> if not found.
        /// Uses a two-call pattern to handle values larger than the initial buffer.
        /// </summary>
        public string? GetVariable(string name)
        {
            byte[] initial = new byte[256];
            nuint actual = KitsuneGetVariable(name, initial, (nuint)initial.Length);
            if (actual == 0)
                return null;
            if (actual < (nuint)initial.Length)
                return Encoding.UTF8.GetString(initial, 0, (int)actual);
            byte[] buffer = new byte[(int)actual + 1];
            KitsuneGetVariable(name, buffer, (nuint)buffer.Length);
            return Encoding.UTF8.GetString(buffer, 0, (int)actual);
        }

        /// <summary>
        /// Returns a global variable as raw bytes, or <c>null</c> if not found.
        /// Uses a two-call pattern to handle values larger than the initial buffer.
        /// </summary>
        public byte[]? GetVariableBytes(string name)
        {
            byte[] initial = new byte[256];
            nuint actual = KitsuneGetVariable(name, initial, (nuint)initial.Length);
            if (actual == 0)
                return null;
            if (actual < (nuint)initial.Length)
                return initial[..(int)actual];
            byte[] buffer = new byte[(int)actual + 1];
            KitsuneGetVariable(name, buffer, (nuint)buffer.Length);
            return buffer[..(int)actual];
        }

        /// <summary>
        /// Returns the byte length of the pending result without consuming it,
        /// or 0 if no result is available.
        /// </summary>
        public bool HasResult() => KitsuneHasResult() != 0;

        /// <summary>
        /// Waits for a result and returns it as raw bytes, or <c>null</c> if none available.
        /// Consumes the result.
        /// </summary>
        public byte[]? GetResult()
        {
            nuint len = KitsuneHasResult();
            if (len == 0)
                return null;
            byte[] buffer = new byte[(int)len + 1];
            nuint actual = KitsuneGetResult(buffer, (nuint)buffer.Length);
            return actual == 0 ? null : buffer[..(int)actual];
        }

        /// <summary>
        /// Waits for a result and returns it as a UTF-8 string, or <c>null</c> if none available.
        /// Consumes the result.
        /// </summary>
        public string? GetResultString()
        {
            byte[]? result = GetResult();
            return result is null ? null : Encoding.UTF8.GetString(result);
        }

        public void Dispose()
        {
            if (!_disposed)
            {
                KitsuneCleanup();
                _disposed = true;
            }
        }
    }
}
