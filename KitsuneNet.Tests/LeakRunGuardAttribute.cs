using System.Reflection;
using KitsuneNet;
using Xunit.Sdk;

[assembly: KitsuneNet.Tests.LeakRunGuard]

namespace KitsuneNet.Tests;

/// <summary>
/// Active only when the environment variable <c>KITSUNE_LEAK_RUN</c> is <c>1</c> (set by <c>test-leaks.ps1</c>,
/// which runs tests one at a time). The native leak check runs when the last engine is disposed, so it only
/// covers a test if no engine is alive before the test starts and none is left when it ends.
/// </summary>
[AttributeUsage(AttributeTargets.Assembly | AttributeTargets.Class | AttributeTargets.Method)]
public sealed class LeakRunGuardAttribute : BeforeAfterTestAttribute
{
    private static readonly bool Active = Environment.GetEnvironmentVariable("KITSUNE_LEAK_RUN") == "1";

    public override void Before(MethodInfo methodUnderTest)
    {
        if (Active && KitsuneEngine.GetReferences() != 0)
        {
            throw new InvalidOperationException($"{KitsuneEngine.GetReferences()} engine(s) alive before the test started; an earlier test did not dispose its engine");
        }
    }

    public override void After(MethodInfo methodUnderTest)
    {
        if (Active && KitsuneEngine.GetReferences() != 0)
        {
            throw new InvalidOperationException($"{KitsuneEngine.GetReferences()} engine(s) still alive after the test; dispose every engine so the leak check runs");
        }
    }
}
