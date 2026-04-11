using System;

namespace KitsuneNet
{
    /// <summary>
    /// Marks a public instance method to be exposed as a Lua method when the declaring class
    /// is registered via <see cref="KitsuneEngine.RegisterUserdata{T}"/>.
    /// The method must have the signature <c>LuaValue MethodName(IReadOnlyList&lt;LuaValue&gt; args)</c>.
    /// <c>args[0]</c> is the self argument (the userdata instance).
    /// </summary>
    [AttributeUsage(AttributeTargets.Method)]
    public sealed class LuaMethodAttribute : Attribute
    {
        /// <summary>
        /// The name exposed to Lua. When null the method's C# name is used.
        /// </summary>
        public string? Name { get; init; }
    }
}
