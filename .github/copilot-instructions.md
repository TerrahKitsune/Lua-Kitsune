# Copilot Instructions

## Project Guidelines
- When updating documentation files (especially `Lua functions.md`), use proper Markdown structure/markup instead of plain text formatting. Avoid broad replacements; preserve all existing sections and verify full-file integrity after edits.
- The `tests/*.lua` files in Lua-Kitsune do not exist and are not used. All tests are C# xUnit tests located in `KitsuneNet.Tests\KitsuneUtilTests.cs` (and related `*Tests.cs` files). Never reference or create `tests/*.lua` files.

## C++ Code Style

### Indentation
- Use **tabs** for indentation throughout all C++ files.

### Brace style
- **Never** put a multi-statement block on a single line. Every braced block body must be on its own line(s):
  ```cpp
  // Wrong
  if (cond) { stmt1; stmt2; }

  // Correct
  if (cond) {
      stmt1;
      stmt2;
  }
  ```
- Single-statement `if`/`while`/`for` bodies **without** braces must still have the body on its own line:
  ```cpp
  // Wrong
  if (!p) return NULL;
  if (j->recLen > 0) j->recLen--;

  // Correct
  if (j->recLen > 0)
      j->recLen--;
  ```
- When a body has two or more statements, always use braces and expand fully:
  ```cpp
  if (j->recLen > 0) {
      j->recLen--;
      SomeOtherThing();
  }
  ```
- Function bodies that contain only one or two simple statements should still follow the multi-line rule if the body uses braces:
  ```cpp
  // Wrong
  static void enc_reset(LuaJson* j) { j->outLen = 0; j->recLen = 0; }

  // Correct
  static void enc_reset(LuaJson* j) {
      j->outLen = 0;
      j->recLen = 0;
  }
  ```

### Lua module registration
- Follow the **wchar pattern**: one `functions[]` table containing all callable methods, one `meta[]` table containing only metamethods (`__gc`, `__tostring`, etc.), and `__index = module table` so all functions are reachable both as `Module.Xxx()` and `instance:Xxx()`.
- Do **not** create a separate `__index` subtable with a filtered subset of methods.

## C# Code Style
- When writing C# code for the Lua-Kitsune project, follow the rules defined in `analysis.ruleset`. Key rules include:
  - UseConfigureAwait (Warning)
  - CA1001 (Warning)
  - CA1063 (Warning)
  - IDE0052 (Warning)
  - IDE0180 (Warning)
  - IDE0033 (Warning)
  - SA1412 (Warning)
  - StyleCop/SA rules
- CS1591 (missing XML doc) is None (suppressed).
- SA1101 (this. prefix) is None.
- SA1200 (using directives placement) is None.
- SA1309 (field name prefix) is None.
- SA1310 (field name underscore) is None.
- SA1600 (public member docs) is None.
