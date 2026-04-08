namespace KitsuneNet
{
    /// <summary>
    /// A C# function that can be registered and called from Lua as <c>Kitsune.Name(...)</c>.
    /// </summary>
    /// <param name="args">The Lua call arguments. Valid only for the duration of the call.</param>
    /// <returns>
    /// The value to return to Lua, or <see cref="LuaValue.None"/> to return nothing.
    /// Throw <see cref="LuaException"/> (or any exception) to raise a Lua error.
    /// </returns>
    public delegate LuaValue LuaFunction(IReadOnlyList<LuaValue> args);
}
