namespace KitsuneNet
{
    /// <summary>Thrown when a Lua script raises a runtime or syntax error.</summary>
    public sealed class LuaException : Exception
    {
        public LuaException(string message)
            : base(message)
        {
        }
    }
}
