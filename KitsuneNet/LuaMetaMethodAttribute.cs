using System;

namespace KitsuneNet
{
    /// <summary>
    /// Marks a public instance method to be exposed as a Lua metamethod when the declaring class
    /// is registered via <see cref="KitsuneEngine.RegisterUserdata{T}"/>.
    /// The method must have the signature <c>LuaValue MethodName(IReadOnlyList&lt;LuaValue&gt; args)</c>.
    /// Metamethod names should match Lua conventions, e.g. <c>__tostring</c>, <c>__add</c>.
    /// <c>__gc</c> is handled automatically; a user-supplied <c>__gc</c> is called before the
    /// engine frees the GCHandle.
    /// </summary>
    [AttributeUsage(AttributeTargets.Method)]
    public sealed class LuaMetaMethodAttribute : Attribute
    {
        public LuaMetaMethodAttribute(string name)
        {
            Name = name;
        }

        /// <summary>The Lua metamethod name, e.g. <c>__tostring</c>.</summary>
        public string Name { get; }
    }
}
