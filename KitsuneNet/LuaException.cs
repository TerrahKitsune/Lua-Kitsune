namespace KitsuneNet
{
    /// <summary>Thrown when a Lua script raises a runtime or syntax error.</summary>
    public class LuaException : Exception
    {
        public LuaException(string message)
            : base(message)
        {
        }
    }

    /// <summary>Thrown when a Lua script was stopped by <c>os.exit</c>. The engine keeps running and
    /// the process is not ended; the host decides what to do with <see cref="ExitCode"/>.</summary>
    public sealed class LuaExitException : LuaException
    {
        public LuaExitException(int exitCode)
            : base($"Lua script called os.exit({exitCode})")
        {
            ExitCode = exitCode;
        }

        /// <summary>The code passed to <c>os.exit</c>: an integer, 0 for <c>true</c> or no argument, 1 for <c>false</c>.</summary>
        public int ExitCode { get; }
    }
}
