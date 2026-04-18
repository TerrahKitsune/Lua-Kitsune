#!/usr/bin/env python3
"""
generate_imgui_bindings.py

Reads Kitsune/Imgui/dcimgui.json (produced by dear_bindings from imgui.h)
and emits:
  - Kitsune/ImguiRenderer_generated.cpp   (kitsune_CFunction wrappers + add_imgui_bindings)
  - Kitsune/ImguiEnums_generated.cpp      (build_imgui_enum_table implementation)
  - docs/imgui-renderer-api.md            (Markdown API reference)

Run from the repo root:
  python Kitsune/generate_imgui_bindings.py
"""

import json
import os
import sys

SCRIPT_DIR   = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT    = os.path.dirname(SCRIPT_DIR)
DCIMGUI_JSON = os.path.join(SCRIPT_DIR, "Imgui", "dcimgui.json")
OVERRIDES    = os.path.join(SCRIPT_DIR, "overrides.json")
OUT_RENDERER = os.path.join(SCRIPT_DIR, "ImguiRenderer_generated.cpp")
OUT_ENUMS    = os.path.join(SCRIPT_DIR, "ImguiEnums_generated.cpp")
OUT_DOCS     = os.path.join(SCRIPT_DIR,  "docs", "imgui-renderer-api.md")

# ---------------------------------------------------------------------------
# Type mapping
# ---------------------------------------------------------------------------

# Maps a dcimgui C type string to (unpack_expr, setter_type, setter_field)
# unpack_expr uses {i} for argument index.
# setter_type is the KITSUNE_T* constant name.
# setter_field is the KitsuneVariable union field name.
TYPE_MAP = {
    "const char*":  ("string",   "KITSUNE_TSTRING",  "data"),
    "bool":         ("bool",     "KITSUNE_TBOOLEAN", "boolean"),
    "int":          ("int",      "KITSUNE_TINTEGER", "integer"),
    "unsigned int": ("uint",     "KITSUNE_TINTEGER", "integer"),
    "float":        ("float",    "KITSUNE_TNUMBER",  "number"),
    "double":       ("double",   "KITSUNE_TNUMBER",  "number"),
    "ImVec2":       ("imvec2",   None,               None),
    "ImVec4":       ("imvec4",   None,               None),
    "bool*":        ("bool_out", "KITSUNE_TBOOLEAN", "boolean"),
    "float*":       ("float_out","KITSUNE_TNUMBER",  "number"),
    "int*":         ("int_out",  "KITSUNE_TINTEGER", "integer"),
}

UNSUPPORTED_TYPES = {
    "void*", "ImGuiInputTextCallback", "ImGuiSizeCallback",
    "ImGuiPayload*", "ImDrawList*", "ImFont*", "ImFontAtlas*",
    "va_list", "...", "ImGuiStorage*", "ImGuiViewport*",
    "ImGuiTableSortSpecs*", "ImGuiTableBgTarget",
    "ImTextureID", "const ImVec2*", "size_t", "size_t*",
    "void**", "ImGuiSelectionUserData",
}


def load_json(path):
    with open(path, "r", encoding="utf-8-sig") as f:
        return json.load(f)


def get_type_str(type_obj):
    """Extract a normalised type string from a dcimgui type object."""
    if isinstance(type_obj, str):
        return type_obj.strip()
    if isinstance(type_obj, dict):
        return type_obj.get("declaration", "").strip()
    return ""


def is_unsupported(type_str):
    for u in UNSUPPORTED_TYPES:
        if u in type_str:
            return True
    return False


def is_nullable(param):
    return param.get("is_nullable", False)


def emit_string_unpack(i, var_name):
    """Emit the fast/slow path for const char* parameters."""
    return (
        f"\tconst char* {var_name};\n"
        f"\tKitsuneVariable* {var_name}Owned = nullptr;\n"
        f"\tif (argv[{i}].type == KITSUNE_TSTRING) {{\n"
        f"\t\t{var_name} = (const char*)argv[{i}].data;\n"
        f"\t}} else {{\n"
        f"\t\t{var_name}Owned = KitsuneToString(&argv[{i}]);\n"
        f"\t\t{var_name} = {var_name}Owned ? (const char*){var_name}Owned->data : \"\";\n"
        f"\t}}\n"
    )


def emit_string_free(var_name):
    return f"\tif ({var_name}Owned) KitsuneVariableFree({var_name}Owned);\n"


# ---------------------------------------------------------------------------
# Lua signature builder (for docs)
# ---------------------------------------------------------------------------

# Map C type -> Lua type name for docs
LUA_TYPE_NAMES = {
    "const char*": "string",
    "bool":        "boolean",
    "bool*":       "boolean",
    "int":         "integer",
    "unsigned int":"integer",
    "ImGuiID":     "integer",
    "float":       "number",
    "float*":      "number",
    "double":      "number",
    "int*":        "integer",
    "ImVec2":      "number, number  or  {x, y}",
    "ImVec4":      "number, number, number, number",
    "const float*":"table",
    "void":        "",
}

def lua_type(ts):
    if ts in LUA_TYPE_NAMES:
        return LUA_TYPE_NAMES[ts]
    if ts.startswith("ImGui") and (ts.endswith("_") or ts in (
            "ImGuiMouseButton","ImGuiKey","ImGuiCol","ImGuiDir",
            "ImGuiMouseCursor","ImGuiKeyChord","ImGuiCond",
            "ImGuiDataType","ImGuiSortDirection")):
        return "integer"
    if ts.endswith("Flags") or ts.endswith("Flags_"):
        return "integer"
    return ts

def build_func_doc(func, lua_name):
    """Returns (signature_str, returns_str) for docs."""
    params = func.get("arguments", [])
    ret_str = get_type_str(func.get("return_type", {}))

    param_parts = []
    for p in params:
        ts    = get_type_str(p.get("type", {}))
        pname = p.get("name", "?")
        dflt  = p.get("default_value")
        nullable = is_nullable(p)
        lt = lua_type(ts)
        if dflt is not None or nullable:
            param_parts.append(f"[{pname}: {lt}]")
        else:
            param_parts.append(f"{pname}: {lt}")

    sig = f"renderer:{lua_name}({', '.join(param_parts)})"

    # Build returns string
    returns = []
    lt_ret = lua_type(ret_str)
    if lt_ret:
        returns.append(lt_ret)
    if ret_str == "ImVec2":
        returns = ["number x", "number y"]
    elif ret_str == "ImVec4":
        returns = ["number r", "number g", "number b", "number a"]

    for p in params:
        ts = get_type_str(p.get("type", {}))
        pname = p.get("name", "?")
        nullable = is_nullable(p)
        if ts in ("bool*", "float*", "int*"):
            lt = lua_type(ts)
            if nullable:
                returns.append(f"{lt} {pname} (if provided)")
            else:
                returns.append(f"{lt} {pname}")

    returns_str = ", ".join(returns) if returns else "—"
    return sig, returns_str


# ---------------------------------------------------------------------------
# Wrapper code generation
# ---------------------------------------------------------------------------

def generate_wrapper(func, overrides):
    """
    Returns (code_str, skip_reason) where skip_reason is None if supported.
    """
    name = func.get("name", "")
    if name in overrides:
        return None, f"hand-written override"

    # Use the original C++ name (e.g. "ImGui::SameLine") for the actual call,
    # stripping the dcimgui disambiguation suffixes (Ex, Str, Int, etc.)
    original = func.get("original_fully_qualified_name", "")
    if original.startswith("ImGui::"):
        cpp_call_name = original[len("ImGui::"):]
    else:
        cpp_call_name = name

    params = func.get("arguments", [])
    ret    = func.get("return_type", {})
    ret_str = get_type_str(ret)

    # Check for unsupported parameter types
    for p in params:
        ts = get_type_str(p.get("type", {}))
        if is_unsupported(ts):
            return None, f"unsupported parameter type: {ts}"
    if is_unsupported(ret_str):
        return None, f"unsupported return type: {ret_str}"
    # Skip reference, struct, and complex return types we can't map
    if (ret_str.endswith("&") or
            (ret_str.endswith("*") and ret_str != "const char*") or
            ret_str in ("ImGuiIO", "ImGuiStyle", "ImGuiPlatformIO", "ImGuiWindowClass",
                        "ImGuiPlatformImeData", "ImGuiPlatformMonitor",
                        "ImGuiMultiSelectIO*", "ImGuiTableSortSpecs*", "ImGuiPayload*",
                        "ImGuiViewport*", "ImDrawList*", "ImGuiStorage*", "ImGuiDrawListSharedData*")):
        return None, f"unsupported return type: {ret_str}"

    lines = []
    lines.append(f"static int ImguiRenderer_{name}(int argc, const KitsuneVariable* argv,")
    lines.append(f"\tconst kitsune_ResultSetter setter, void* ud) {{")
    lines.append(f"\tImguiWindowContext* ctx = (ImguiWindowContext*)ud;")
    # argv[0] is the renderer userdata (self) from Lua colon-call syntax — skip it.
    lines.append(f"\tconst int _argc = argc - 1;")
    lines.append(f"\tconst KitsuneVariable* _argv = argc > 0 ? argv + 1 : argv;")
    lines.append(f"\t(void)ctx;")

    # Build argc check: count required argv slots (ImVec2 = 2 slots, ImVec4 = 4)
    required_count = 0
    hit_default = False
    for p in params:
        if hit_default:
            break
        if p.get("default_value") is None:
            ts = get_type_str(p.get("type", {}))
            if ts == "ImVec2":
                required_count += 2
            elif ts == "ImVec4":
                required_count += 4
            else:
                required_count += 1
        else:
            hit_default = True

    if required_count > 0:
        lines.append(f"\tif (_argc < {required_count}) {{")
        lines.append(f"\t\tKitsuneVariable err = {{}};")
        lines.append(f"\t\tconst char* msg = \"{name} requires {required_count} argument(s)\";")
        lines.append(f"\t\terr.type = KITSUNE_TERROR; err.data = (unsigned char*)msg; err.length = strlen(msg);")
        lines.append(f"\t\tsetter(&err); return 1;")
        lines.append(f"\t}}")

    # Unpack arguments and build the call
    call_args = []
    out_vars  = []
    cleanup   = []
    i = 0

    for p in params:
        ts    = get_type_str(p.get("type", {}))
        pname = p.get("name", f"p{i}")
        dflt  = p.get("default_value")
        nullable = is_nullable(p)
        has_arg = f"_argc > {i}"

        if ts == "const char*":
            if dflt is not None:
                lines.append(f"\t// arg {i}: {pname} (optional, default={dflt})")
                lines.append(f"\tconst char* {pname} = nullptr;")
                lines.append(f"\tKitsuneVariable* {pname}Owned = nullptr;")
                lines.append(f"\tif ({has_arg}) {{")
                lines.append(f"\t\tif (argv[{i}].type == KITSUNE_TSTRING) {pname} = (const char*)argv[{i}].data;")
                lines.append(f"\t\telse {{ {pname}Owned = KitsuneToString(&argv[{i}]); {pname} = {pname}Owned ? (const char*){pname}Owned->data : \"\"; }}")
                lines.append(f"\t}}")
                cleanup.append(f"\tif ({pname}Owned) KitsuneVariableFree({pname}Owned);")
            else:
                lines.append(emit_string_unpack(i, pname))
                cleanup.append(emit_string_free(pname).rstrip("\n"))
            call_args.append(pname)

        elif ts == "bool":
            if dflt is not None:
                lines.append(f"\tbool {pname} = {has_arg} ? KitsuneAsBool(&argv[{i}]) : {dflt};")
            else:
                lines.append(f"\tbool {pname} = KitsuneAsBool(&argv[{i}]);")
            call_args.append(pname)

        elif ts in ("int", "unsigned int") or ts.endswith("Flags") or ts.endswith("Flags_"):
            if dflt is not None:
                lines.append(f"\tint {pname} = {has_arg} ? (int)KitsuneAsInt(&argv[{i}], {dflt}) : {dflt};")
            else:
                lines.append(f"\tint {pname} = (int)KitsuneAsInt(&argv[{i}], 0);")
            call_args.append(pname)

        elif ts == "ImGuiID" or ts == "ImU32":
            if dflt is not None:
                lines.append(f"\t{ts} {pname} = {has_arg} ? ({ts})(unsigned int)KitsuneAsInt(&argv[{i}], {dflt}) : ({ts}){dflt};")
            else:
                lines.append(f"\tif (argv[{i}].type != KITSUNE_TINTEGER) {{ KitsuneVariable _err = {{}}; const char* _m = \"{name}: {pname} must be an integer\"; _err.type = KITSUNE_TERROR; _err.data = (unsigned char*)_m; _err.length = strlen(_m); setter(&_err); return 1; }}")
                lines.append(f"\t{ts} {pname} = ({ts})(unsigned int)KitsuneAsInt(&argv[{i}], 0);")
            call_args.append(pname)

        elif ts.startswith("ImGui") and (ts.endswith("_") or ts in (
                "ImGuiMouseButton", "ImGuiKey", "ImGuiCol", "ImGuiDir",
                "ImGuiMouseCursor", "ImGuiKeyChord", "ImGuiCond",
                "ImGuiDataType", "ImGuiSortDirection")):
            if dflt is not None:
                lines.append(f"\t{ts} {pname} = {has_arg} ? ({ts})(int)KitsuneAsInt(&argv[{i}], {dflt}) : ({ts}){dflt};")
            else:
                lines.append(f"\t{ts} {pname} = ({ts})(int)KitsuneAsInt(&argv[{i}], 0);")
            call_args.append(pname)

        elif ts == "float":
            if dflt is not None:
                dflt_clean = str(dflt).rstrip('f').rstrip('F')
                if dflt_clean.upper().startswith('FLT_') or dflt_clean.upper().startswith('DBL_'):
                    dflt_clean = str(dflt)
                lines.append(f"\tfloat {pname} = {has_arg} ? KitsuneAsFloat(&argv[{i}], {dflt_clean}f) : {dflt_clean}f;")
            else:
                lines.append(f"\tfloat {pname} = KitsuneAsFloat(&argv[{i}], 0.0f);")
            call_args.append(pname)

        elif ts == "double":
            if dflt is not None:
                lines.append(f"\tdouble {pname} = {has_arg} ? KitsuneAsDouble(&argv[{i}], {dflt}) : {dflt};")
            else:
                lines.append(f"\tdouble {pname} = KitsuneAsDouble(&argv[{i}], 0.0);")
            call_args.append(pname)

        elif ts == "const float*":
            lines.append(f"\tint {pname}_count = 0;")
            lines.append(f"\tfloat* {pname} = nullptr;")
            lines.append(f"\tif ({has_arg}) {{")
            lines.append(f"\t\tif (argv[{i}].type != KITSUNE_TTABLE) {{")
            lines.append(f"\t\t\tKitsuneVariable _err = {{}}; const char* _m = \"{name}: {pname} must be a sequential table of numbers\";")
            lines.append(f"\t\t\t_err.type = KITSUNE_TERROR; _err.data = (unsigned char*)_m; _err.length = strlen(_m);")
            lines.append(f"\t\t\tsetter(&_err); return 1;")
            lines.append(f"\t\t}}")
            lines.append(f"\t\tKitsuneVariable* _len = KitsuneGetLength(&argv[{i}]);")
            lines.append(f"\t\t{pname}_count = _len ? (int)KitsuneAsInt(_len, 0) : 0;")
            lines.append(f"\t\tKitsuneVariableFree(_len);")
            lines.append(f"\t\tif ({pname}_count > 0) {{")
            lines.append(f"\t\t\t{pname} = (float*)alloca({pname}_count * sizeof(float));")
            lines.append(f"\t\t\tfor (int _k = 0; _k < {pname}_count; _k++) {{")
            lines.append(f"\t\t\t\tKitsuneVariable _ki = {{}}; _ki.type = KITSUNE_TINTEGER; _ki.integer = _k + 1;")
            lines.append(f"\t\t\t\tKitsuneVariable* _kv = KitsuneGetIndex(&argv[{i}], &_ki);")
            lines.append(f"\t\t\t\tif (!_kv || (_kv->type != KITSUNE_TNUMBER && _kv->type != KITSUNE_TINTEGER)) {{")
            lines.append(f"\t\t\t\t\tKitsuneVariableFree(_kv);")
            lines.append(f"\t\t\t\t\tKitsuneVariable _err = {{}}; const char* _m = \"{name}: {pname} must be a sequential table of numbers\";")
            lines.append(f"\t\t\t\t\t_err.type = KITSUNE_TERROR; _err.data = (unsigned char*)_m; _err.length = strlen(_m);")
            lines.append(f"\t\t\t\t\tsetter(&_err); return 1;")
            lines.append(f"\t\t\t\t}}")
            lines.append(f"\t\t\t\t{pname}[_k] = KitsuneAsFloat(_kv, 0.0f);")
            lines.append(f"\t\t\t\tKitsuneVariableFree(_kv);")
            lines.append(f"\t\t\t}}")
            lines.append(f"\t\t}}")
            lines.append(f"\t}}")
            call_args.append(pname)

        elif ts == "ImVec2":
            # Always consumes 2 argv slots (matching ImVec4 consuming 4).
            # If argv[i] is a table, read x/y from keys [1][2] and ignore argv[i+1].
            # If argv[i] is a number, read argv[i] as x and argv[i+1] as y.
            lines.append(f"\tImVec2 {pname}(0.0f, 0.0f);")
            lines.append(f"\tif ({has_arg} && argv[{i}].type == KITSUNE_TTABLE) {{")
            lines.append(f"\t\tKitsuneVariable _k1 = {{}}; _k1.type = KITSUNE_TINTEGER; _k1.integer = 1;")
            lines.append(f"\t\tKitsuneVariable _k2 = {{}}; _k2.type = KITSUNE_TINTEGER; _k2.integer = 2;")
            lines.append(f"\t\tKitsuneVariable* _v1 = KitsuneGetIndex(&argv[{i}], &_k1);")
            lines.append(f"\t\tKitsuneVariable* _v2 = KitsuneGetIndex(&argv[{i}], &_k2);")
            lines.append(f"\t\t{pname}.x = _v1 ? KitsuneAsFloat(_v1, 0.0f) : 0.0f;")
            lines.append(f"\t\t{pname}.y = _v2 ? KitsuneAsFloat(_v2, 0.0f) : 0.0f;")
            lines.append(f"\t\tKitsuneVariableFree(_v1); KitsuneVariableFree(_v2);")
            lines.append(f"\t}} else if ({has_arg}) {{")
            lines.append(f"\t\t{pname}.x = KitsuneAsFloat(&argv[{i}], 0.0f);")
            lines.append(f"\t\t{pname}.y = _argc > {i+1} ? KitsuneAsFloat(&argv[{i+1}], 0.0f) : 0.0f;")
            lines.append(f"\t}}")
            i += 1  # always consumes 2 slots total (i is incremented once more at bottom of loop)
            call_args.append(pname)

        elif ts == "ImVec4":
            lines.append(f"\tImVec4 {pname} = ({has_arg} && _argc > {i+3})")
            lines.append(f"\t\t? ImVec4(KitsuneAsFloat(&argv[{i}],0),KitsuneAsFloat(&argv[{i+1}],0),KitsuneAsFloat(&argv[{i+2}],0),KitsuneAsFloat(&argv[{i+3}],0))")
            lines.append(f"\t\t: ImVec4(0,0,0,1);")
            i += 3
            call_args.append(pname)

        elif ts == "bool*":
            # Always treat bool* as nullable — passing nullptr is always valid for ImGui bool* params.
            # Only create a non-null pointer if the caller explicitly passes a boolean.
            lines.append(f"\tbool {pname}_v = false;")
            lines.append(f"\tbool* {pname} = ({has_arg} && _argv[{i}].type == KITSUNE_TBOOLEAN) ? &{pname}_v : nullptr;")
            lines.append(f"\tif ({pname}) {pname}_v = KitsuneAsBool(&_argv[{i}]);")
            out_vars.append((f"{pname}", "KITSUNE_TBOOLEAN", "boolean", f"{pname}_v", True))
            call_args.append(pname)

        elif ts == "float*":
            # Check if the C++ signature actually takes float& (dcimgui uses float* for both)
            if nullable:
                lines.append(f"\tfloat {pname}_v = 0.0f;")
                lines.append(f"\tfloat* {pname} = ({has_arg} && argv[{i}].type != KITSUNE_TNIL) ? &{pname}_v : nullptr;")
                lines.append(f"\tif ({pname}) {pname}_v = KitsuneAsFloat(&argv[{i}], 0.0f);")
                out_vars.append((f"{pname}", "KITSUNE_TNUMBER", "number", f"(double){pname}_v", nullable))
                call_args.append(pname)
            else:
                lines.append(f"\tfloat {pname}_v = {has_arg} ? KitsuneAsFloat(&argv[{i}], 0.0f) : 0.0f;")
                lines.append(f"\tfloat* {pname} = &{pname}_v;")
                out_vars.append((f"{pname}", "KITSUNE_TNUMBER", "number", f"(double){pname}_v", False))
                call_args.append(pname)

        elif ts == "int*":
            if nullable:
                lines.append(f"\tint {pname}_v = 0;")
                lines.append(f"\tint* {pname} = ({has_arg} && argv[{i}].type != KITSUNE_TNIL) ? &{pname}_v : nullptr;")
                lines.append(f"\tif ({pname}) {pname}_v = (int)KitsuneAsInt(&argv[{i}], 0);")
                out_vars.append((f"{pname}", "KITSUNE_TINTEGER", "integer", f"(long long){pname}_v", nullable))
            else:
                lines.append(f"\tint {pname}_v = {has_arg} ? (int)KitsuneAsInt(&argv[{i}], 0) : 0;")
                lines.append(f"\tint* {pname} = &{pname}_v;")
                out_vars.append((f"{pname}", "KITSUNE_TINTEGER", "integer", f"(long long){pname}_v", False))
            call_args.append(pname)

        else:
            # Fallback: skip unknown type
            return None, f"unsupported parameter type: {ts}"

        i += 1

    # Emit cleanup before the call
    for c in cleanup:
        pass  # cleanup is emitted after the call below

    # Build the ImGui function call using the original C++ name
    call_expr = f"ImGui::{cpp_call_name}({', '.join(call_args)})"

    ret_count = 0
    if ret_str == "void" or ret_str == "":
        lines.append(f"\t{call_expr};")
        ret_count = 0
    elif ret_str == "bool":
        lines.append(f"\tbool _ret = {call_expr};")
        lines.append(f"\tKitsuneVariable r0 = {{}}; r0.type = KITSUNE_TBOOLEAN; r0.boolean = _ret;")
        lines.append(f"\tsetter(&r0);")
        ret_count = 1
    elif ret_str in ("int", "unsigned int", "ImGuiID", "ImU32") or (ret_str.startswith("ImGui") and not ret_str.endswith("&")):
        lines.append(f"\tint _ret = (int)({call_expr});")
        lines.append(f"\tKitsuneVariable r0 = {{}}; r0.type = KITSUNE_TINTEGER; r0.integer = (long long)_ret;")
        lines.append(f"\tsetter(&r0);")
        ret_count = 1
    elif ret_str == "float":
        lines.append(f"\tfloat _ret = {call_expr};")
        lines.append(f"\tKitsuneVariable r0 = {{}}; r0.type = KITSUNE_TNUMBER; r0.number = (double)_ret;")
        lines.append(f"\tsetter(&r0);")
        ret_count = 1
    elif ret_str == "double":
        lines.append(f"\tdouble _ret = {call_expr};")
        lines.append(f"\tKitsuneVariable r0 = {{}}; r0.type = KITSUNE_TNUMBER; r0.number = _ret;")
        lines.append(f"\tsetter(&r0);")
        ret_count = 1
    elif ret_str == "const char*":
        lines.append(f"\tconst char* _ret = {call_expr};")
        lines.append(f"\tKitsuneVariable r0 = {{}}; r0.type = KITSUNE_TSTRING;")
        lines.append(f"\tr0.data = _ret ? (unsigned char*)_ret : (unsigned char*)\"\";")
        lines.append(f"\tr0.length = _ret ? strlen(_ret) : 0;")
        lines.append(f"\tsetter(&r0);")
        ret_count = 1
    elif ret_str == "ImVec2":
        lines.append(f"\tImVec2 _ret = {call_expr};")
        lines.append(f"\tKitsuneVariable rx = {{}}; rx.type = KITSUNE_TNUMBER; rx.number = (double)_ret.x; setter(&rx);")
        lines.append(f"\tKitsuneVariable ry = {{}}; ry.type = KITSUNE_TNUMBER; ry.number = (double)_ret.y; setter(&ry);")
        ret_count = 2
    elif ret_str == "ImVec4":
        lines.append(f"\tImVec4 _ret = {call_expr};")
        lines.append(f"\tKitsuneVariable rr = {{}}; rr.type = KITSUNE_TNUMBER; rr.number = (double)_ret.x; setter(&rr);")
        lines.append(f"\tKitsuneVariable rg = {{}}; rg.type = KITSUNE_TNUMBER; rg.number = (double)_ret.y; setter(&rg);")
        lines.append(f"\tKitsuneVariable rb = {{}}; rb.type = KITSUNE_TNUMBER; rb.number = (double)_ret.z; setter(&rb);")
        lines.append(f"\tKitsuneVariable ra = {{}}; ra.type = KITSUNE_TNUMBER; ra.number = (double)_ret.w; setter(&ra);")
        ret_count = 4
    else:
        return None, f"unsupported return type: {ret_str}"

    # Emit cleanup
    for c in cleanup:
        lines.append(c)

    # Emit out vars (use indexed names to avoid redefinition)
    for idx_ro, (ptr_name, ktype, kfield, val_expr, nullable_out) in enumerate(out_vars):
        if nullable_out:
            lines.append(f"\tif ({ptr_name}) {{")
            lines.append(f"\t\tKitsuneVariable ro{idx_ro} = {{}}; ro{idx_ro}.type = {ktype}; ro{idx_ro}.{kfield} = {val_expr}; setter(&ro{idx_ro});")
            lines.append(f"\t}}")
        else:
            lines.append(f"\tKitsuneVariable ro{idx_ro} = {{}}; ro{idx_ro}.type = {ktype}; ro{idx_ro}.{kfield} = {val_expr}; setter(&ro{idx_ro});")
        ret_count += 1

    lines.append(f"\treturn {ret_count};")
    lines.append("}")
    lines.append("")

    raw = "\n".join(lines)
    # The parameter unpack helpers (emit_string_unpack etc.) still emit raw argv[/argc.
    # Replace them with _argv[/_argc, but only in lines that are NOT the function
    # signature or the _argv/_argc initialiser lines (those already use the right names).
    import re
    def fix_line(line):
        if ('const int _argc' in line or
            'const KitsuneVariable* _argv' in line or
            'static int ImguiRenderer_' in line):
            return line
        line = re.sub(r'\bargv\[', '_argv[', line)
        line = re.sub(r'\bargc\b', '_argc', line)
        return line
    result = "\n".join(fix_line(l) for l in raw.split("\n"))
    return result, None


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    if not os.path.exists(DCIMGUI_JSON):
        print(f"ERROR: {DCIMGUI_JSON} not found. "
              "Place the dear_bindings-generated dcimgui.json in Kitsune/Imgui/.", file=sys.stderr)
        sys.exit(1)

    data      = load_json(DCIMGUI_JSON)
    overrides = set(load_json(OVERRIDES).get("overrides", []))

    functions = data.get("functions", [])
    enums     = data.get("enums", [])

    wrappers   = []
    skipped    = []
    fn_names   = []
    fn_docs    = []  # list of (sig, returns) for docs
    seen_cpp_names = set()

    for func in functions:
        name = func.get("name", "")
        if not name.startswith("ImGui_"):
            continue
        lua_name = name[len("ImGui_"):]

        # If multiple dcimgui variants map to the same C++ function, only emit the first
        original = func.get("original_fully_qualified_name", "")
        if original and original in seen_cpp_names:
            skipped.append((lua_name, f"duplicate variant of {original}"))
            continue

        code, reason = generate_wrapper({**func, "name": lua_name}, overrides)
        if code:
            wrappers.append(code)
            fn_names.append(lua_name)
            fn_docs.append(build_func_doc(func, lua_name))
            if original:
                seen_cpp_names.add(original)
        else:
            skipped.append((lua_name, reason))

    # ---- ImguiRenderer_generated.cpp ----
    os.makedirs(os.path.dirname(OUT_RENDERER), exist_ok=True)
    with open(OUT_RENDERER, "w", encoding="utf-8") as f:
        f.write("// AUTO-GENERATED by generate_imgui_bindings.py — do not edit manually.\n")
        f.write("// Re-run the script after updating Imgui/ or dcimgui.json.\n\n")
        f.write("#ifndef _CRT_SECURE_NO_WARNINGS\n#define _CRT_SECURE_NO_WARNINGS\n#endif\n")
        f.write("#ifdef KITSUNE_IMGUI\n\n")
        f.write('#include "ImguiRenderer.h"\n')
        f.write('#include "Imgui/imgui.h"\n')
        f.write('#include "KitsuneEngine.h"\n')
        f.write('#include <cfloat>\n')
        f.write('#ifdef _WIN32\n#include <malloc.h>\n#else\n#include <alloca.h>\n#endif\n\n')

        for w in wrappers:
            f.write(w)
            f.write("\n")

        f.write("void add_imgui_bindings(KitsuneUserDataRegistration* reg) {\n")
        f.write("\tstatic const struct { const char* name; kitsune_CFunction func; } entries[] = {\n")
        for n in fn_names:
            f.write(f'\t\t{{ "{n}", ImguiRenderer_{n} }},\n')
        f.write("\t};\n")
        f.write("\tfor (size_t i = 0; i < sizeof(entries)/sizeof(entries[0]); i++) {\n")
        f.write("\t\tKitsuneNamedFunction* node = (KitsuneNamedFunction*)calloc(1, sizeof(KitsuneNamedFunction));\n")
        f.write("\t\tif (!node) continue;\n")
        f.write("\t\tnode->name     = (char*)entries[i].name;\n")
        f.write("\t\tnode->func     = entries[i].func;\n")
        f.write("\t\tnode->userdata = nullptr;\n")
        f.write("\t\tnode->Next     = reg->Functions;\n")
        f.write("\t\treg->Functions = node;\n")
        f.write("\t}\n}\n\n")
        f.write("#endif // KITSUNE_IMGUI\n")

    print(f"Wrote {OUT_RENDERER} ({len(fn_names)} wrappers, {len(skipped)} skipped)")

    # ---- Build enum map: { "Imgui.Enum.ImGuiWindowFlags.None" -> int_value } ----
    # Enum name:    "ImGuiWindowFlags_"  -> table name "ImGuiWindowFlags"  (strip trailing _)
    # Element name: "ImGuiWindowFlags_None" -> key "None"  (strip "ImGuiWindowFlags_" prefix)
    enum_entries = []  # list of (lua_path, cname)
    for enum in enums:
        raw_enum_name = enum.get("name", "")
        if enum.get("is_internal"):
            continue
        # Strip trailing underscore for the Lua table name
        table_name = raw_enum_name.rstrip("_")
        prefix = raw_enum_name  # e.g. "ImGuiWindowFlags_"
        for val in enum.get("elements", []):
            cname = val.get("name", "")
            if not cname or val.get("is_internal") or val.get("is_count"):
                continue
            # Strip the enum prefix to get the key, e.g. "None"
            key = cname[len(prefix):] if cname.startswith(prefix) else cname
            if not key:
                key = cname
            lua_path = f"Imgui.Enum.{table_name}.{key}"
            enum_entries.append((lua_path, cname, val.get("value", "")))

    # ---- ImguiEnums_generated.cpp ----
    os.makedirs(os.path.dirname(OUT_ENUMS), exist_ok=True)
    with open(OUT_ENUMS, "w", encoding="utf-8") as f:
        f.write("// AUTO-GENERATED by generate_imgui_bindings.py — do not edit manually.\n\n")
        f.write("#ifdef KITSUNE_IMGUI\n\n")
        f.write('#include "KitsuneEngine.h"\n')
        f.write('#include "Imgui/imgui.h"\n\n')
        f.write("void register_imgui_enums() {\n")
        f.write("\tKitsuneVariable v = {};\n")
        f.write("\tv.type = KITSUNE_TINTEGER;\n")
        for lua_path, cname, _val in enum_entries:
            f.write(f'\tv.integer = (long long){cname};\n')
            f.write(f'\tKitsuneSetVariable("{lua_path}", &v);\n')
        f.write("}\n\n")
        f.write("#endif // KITSUNE_IMGUI\n")

    print(f"Wrote {OUT_ENUMS} ({len(enum_entries)} enum entries)")

    # ---- docs/imgui-renderer-api.md ----
    os.makedirs(os.path.dirname(OUT_DOCS), exist_ok=True)
    with open(OUT_DOCS, "w", encoding="utf-8") as f:
        f.write("# ImGui Renderer API Reference\n\n")
        f.write("> **Auto-generated** by `generate_imgui_bindings.py`. Do not edit manually.\n\n")
        f.write("> **Note:** The `renderer` userdata is only valid during the `renderFn` callback passed to `Imgui.Start`. ")
        f.write("Do not store it across frames.\n\n")

        f.write("## Global API\n\n")
        f.write("### `Imgui.Start(title, width, height, renderFn, [context], [onError])`\n\n")
        f.write("Opens the ImGui window and begins the render loop. Blocks until the window is closed or `renderFn` returns `false`.\n\n")
        f.write("- `title` — window title string\n")
        f.write("- `width`, `height` — initial window size in pixels\n")
        f.write("- `renderFn(renderer, context)` — called every frame. Return `false` to close, `true`/nothing to continue.\n")
        f.write("- `context` *(optional)* — any Lua table; passed to `renderFn` every frame. Created empty if omitted.\n")
        f.write("- `onError(err)` *(optional)* — called when `renderFn` raises an error. Return `true` to keep running, `false`/nothing to stop.\n\n")
        f.write("Can only be called once per script run. Removes itself after the first call.\n\n")

        f.write("### `Imgui.Schedule(fn, [args...])`\n\n")
        f.write("Queues `fn` to start as a fire-and-forget coroutine at the end of the current frame. ")
        f.write("Safe to call from inside `renderFn`. Do not use the `renderer` inside a scheduled function — it is only valid during the frame callback.\n\n")

        f.write("## SDL API\n\n")
        f.write("SDL functions are only available inside an active session (after `Imgui.Start`).\n\n")

        f.write("### SDL Window\n\n")
        f.write("Window and display management. All return `nil` if the window does not exist yet.\n\n")
        f.write("| Function | Parameters | Returns | Notes |\n|---|---|---|---|\n")
        f.write("| `SDL.GetWindowWidth()` | — | `integer` | Current window width in pixels |\n")
        f.write("| `SDL.GetWindowHeight()` | — | `integer` | Current window height in pixels |\n")
        f.write("| `SDL.GetWindowX()` | — | `integer` | Window X position on screen |\n")
        f.write("| `SDL.GetWindowY()` | — | `integer` | Window Y position on screen |\n")
        f.write("| `SDL.SetWindowSize(w, h)` | `integer, integer` | — | Resize the window |\n")
        f.write("| `SDL.SetWindowPosition(x, y)` | `integer, integer` | — | Move the window |\n")
        f.write("| `SDL.SetWindowTitle(title)` | `string` | — | Change the window title bar text |\n")
        f.write("| `SDL.IsMinimized()` | — | `boolean` | Whether the window is minimised |\n")
        f.write("| `SDL.IsFocused()` | — | `boolean` | Whether the window has input focus |\n")
        f.write("| `SDL.SetFullscreen(enabled)` | `boolean` | — | Toggle borderless fullscreen |\n")
        f.write("| `SDL.GetMonitor()` | — | `index, name, x, y, width, height, refreshRate` | Info about the monitor the window is on |\n")
        f.write("\n")

        f.write("### SDL Input\n\n")
        f.write("Polled each frame — not event-based. Safe to call at any point inside `renderFn`.\n\n")
        f.write("| Function | Parameters | Returns | Notes |\n|---|---|---|---|\n")
        f.write("| `SDL.GetKeyState(scancode)` | `integer` | `boolean` | Whether a key is currently held. Uses physical SDL_Scancode values (e.g. W=26, Space=44, Up=82, Left=80, Right=79, Down=81) |\n")
        f.write("| `SDL.GetModState()` | — | `table` | `{ shift, ctrl, alt, gui, capslock, numlock }` — boolean fields |\n")
        f.write("| `SDL.GetMouseState()` | — | `x, y, buttons` | Cursor position in window pixels and button bitmask. bit0=left, bit1=middle, bit2=right |\n")
        f.write("| `SDL.GetRelativeMouseState()` | — | `dx, dy, buttons` | Pixel delta since last call and button bitmask. Resets to 0,0 each call |\n")
        f.write("| `SDL.SetRelativeMouseMode(enabled)` | `boolean` | — | Capture and hide the cursor. Use `GetRelativeMouseState` for deltas |\n")
        f.write("| `SDL.WarpMouse(x, y)` | `integer, integer` | — | Move the cursor to a window-relative position |\n")
        f.write("| `SDL.GetNumJoysticks()` | — | `integer` | Number of connected joystick/gamepad devices |\n")
        f.write("| `SDL.GetGamepadAxis(index, axis)` | `integer, integer` | `number` | Axis value normalised to -1.0..1.0. index is 0-based. Axis values match SDL_GameControllerAxis (0=LeftX, 1=LeftY, 2=RightX, 3=RightY, 4=TriggerLeft, 5=TriggerRight) |\n")
        f.write("| `SDL.GetGamepadButton(index, button)` | `integer, integer` | `boolean` | Whether a button is held. index is 0-based. Button values match SDL_GameControllerButton (0=A, 1=B, 2=X, 3=Y) |\n")
        f.write("\n")

        f.write("### SDL Timing\n\n")
        f.write("| Function | Parameters | Returns | Notes |\n|---|---|---|---|\n")
        f.write("| `SDL.GetTicks()` | — | `integer` | Milliseconds since SDL was initialised |\n")
        f.write("| `SDL.GetPerformanceCounter()` | — | `integer` | High-resolution timer counter value |\n")
        f.write("| `SDL.GetPerformanceFrequency()` | — | `integer` | Ticks per second for the performance counter. Use with `GetPerformanceCounter` for sub-millisecond timing |\n")
        f.write("| `SDL.GetFrameTime()` | — | `dt, fps` | `dt` = seconds since last frame (number). `fps` = exponential moving average frame rate. Both values are computed once at the start of each frame by the render loop — safe to call multiple times per frame |\n")
        f.write("\n")

        f.write("## Win32 API\n\n")
        f.write("Console window management. These functions are only compiled on Windows — they do not exist on other platforms.\n\n")
        f.write("| Function | Parameters | Returns | Notes |\n|---|---|---|---|\n")
        f.write("| `Win32.Console(visible)` | `boolean` | `boolean` | Show or hide the console window. Returns actual visibility state |\n")
        f.write("| `Win32.OwnsConsole()` | — | `boolean` | Whether this process owns its console (not launched from a shell) |\n")
        f.write("| `Win32.DestroyConsole()` | — | `boolean` | Permanently detach the console window. Only valid when `Win32.OwnsConsole()` is true |\n")
        f.write("\n")

        f.write("## OpenGL API\n\n")
        f.write("Texture management. All functions require an active session (called after `Imgui.Start`). ")
        f.write("Texture ids (`luaId`) are stable integers assigned by the resource cache.\n\n")

        f.write("### State Machine\n\n")
        f.write("| `luaId` | `glId` | State | Meaning |\n|---|---|---|---|\n")
        f.write("| `0` | `0` | **Tombstone** | Free slot |\n")
        f.write("| `!= 0` | `!= 0` | **Live** | Valid GPU texture |\n")
        f.write("| `!= 0` | `0` | **Sentinel** | Load attempted and failed or `UnloadTexture` called — no retry |\n")
        f.write("\n")

        f.write("### Resource Loader\n\n")
        f.write("```lua\n")
        f.write("OpenGL.SetResourceLoader(\n")
        f.write("    function(source) return Stream.OpenFile(source) end,\n")
        f.write("    function(id, source)  -- optional post-load callback\n")
        f.write("        local data = OpenGL.GetData(id)\n")
        f.write("        if data.width > 1024 then\n")
        f.write("            OpenGL.UnloadTexture(id)  -- sentinel; caller sees placeholder\n")
        f.write("        end\n")
        f.write("    end\n")
        f.write(")\n")
        f.write("```\n\n")

        f.write("### Loading & Lifecycle\n\n")
        f.write("| Function | Parameters | Returns | Notes |\n|---|---|---|---|\n")
        f.write("| `OpenGL.LoadTexture(stream [, source])` | stream, `string?` | `integer` | Decode and upload. If source matches a cached slot, replaces its GL texture keeping the same id |\n")
        f.write("| `OpenGL.ResolveTexture(source)` | `string` | `integer\\|nil` | Cache-hit (live or sentinel) returns id immediately. On miss calls resource loader. Returns `nil` only if source is completely unknown |\n")
        f.write("| `OpenGL.UnloadTexture(id)` | `integer` | — | Free GPU memory, keep slot as sentinel. `ResolveTexture` returns the id without retrying the loader |\n")
        f.write("| `OpenGL.DestroyTexture(id)` | `integer` | — | Full tombstone — slot freed for reuse. Caller responsible for re-load loops if called from post-loader |\n")
        f.write("| `OpenGL.DestroyAllTextures()` | — | — | Free all textures and zero the resource cache |\n")
        f.write("| `OpenGL.SetResourceLoader(loader [, postLoader])` | `function, function?` | — | Set the session-wide resource loader and optional post-load callback |\n")
        f.write("\n")

        f.write("### Queries\n\n")
        f.write("| Function | Parameters | Returns | Notes |\n|---|---|---|---|\n")
        f.write("| `OpenGL.GetId(source)` | `string` | `integer\\|nil` | Cached id for source without triggering a load |\n")
        f.write("| `OpenGL.GetData(id)` | `integer` | `table\\|nil` | `{ width, height, source, isLoaded }`. `isLoaded` is false for sentinels |\n")
        f.write("| `OpenGL.IsLoaded(id)` | `integer` | `boolean` | True if live (`glId != 0`). False for sentinel or unknown id |\n")
        f.write("| `OpenGL.GetTextureCount()` | — | `count, bytes` | Live+sentinel slot count and approximate GPU byte usage (w×h×4 per live slot) |\n")
        f.write("| `OpenGL.GetAllLoadedTextures()` | — | `table` | Sequential table of all valid luaIds (live and sentinel) |\n")
        f.write("\n")

        f.write("### Manipulation\n\n")
        f.write("| Function | Parameters | Returns | Notes |\n|---|---|---|---|\n")
        f.write("| `OpenGL.ResizeTexture(id, newW, newH [, source])` | `integer, integer, integer, string?` | `integer\\|nil` | GPU blit. No source = in-place. With source = new slot. Source already cached = return existing id |\n")
        f.write("| `OpenGL.CopyTexture(id, newSource)` | `integer, string` | `integer\\|nil` | Blit to newSource. If newSource exists its GL texture is replaced. Error if newSource matches id's own source |\n")
        f.write("\n")

        f.write("### Sprite Sheets\n\n")
        f.write("| Function | Parameters | Returns | Notes |\n|---|---|---|---|\n")
        f.write("| `OpenGL.GetFrameUVs(id, frameIndex, cols, rows)` | `integer, integer, integer, integer` | `u0, v0, u1, v1` | Normalised UV coordinates for a frame. frameIndex is 1-based. Works on sentinels |\n")
        f.write("\n")
        f.write("```lua\n")
        f.write("-- Typical sprite sheet animation\n")
        f.write("renderer:ImageFrame(sheetId, 64, 64, frame, 8, 2)            -- normal\n")
        f.write("renderer:ImageFrame(sheetId, 64, 64, frame, 8, 2, true)      -- flipX (mirror)\n")
        f.write("renderer:ImageFrame(sheetId, 64, 64, frame, 8, 2, false, true) -- flipY\n")
        f.write("```\n\n")

        f.write("## renderer Methods\n\n")
        f.write("Available inside the `renderFn` callback. The renderer userdata is only valid during that callback.\n\n")

        f.write("### Image Methods\n\n")
        f.write("| Function | Parameters | Returns | Notes |\n|---|---|---|---|\n")
        f.write("| `renderer:Image(id, w, h [, u0, v0, u1, v1])` | `integer, number, number, number?, number?, number?, number?` | — | Render a texture. UV defaults to full texture (0,0,1,1). Silently renders blank if id is invalid or sentinel |\n")
        f.write("| `renderer:ImageFrame(id, w, h, frameIndex, cols, rows [, flipX [, flipY]])` | `integer, number, number, integer, integer, integer, boolean?, boolean?` | — | Render one frame of a sprite sheet. flipX mirrors horizontally, flipY mirrors vertically |\n")
        f.write("\n")

        f.write("### Auto-generated ImGui Methods\n\n")
        f.write("| Signature | Returns |\n|---|---|\n")
        for (sig, returns) in fn_docs:
            f.write(f"| `{sig}` | `{returns}` |\n")
        f.write("\n## Hand-written ImGui Overrides\n\n")
        f.write("These ImGui renderer methods are implemented manually in `ImguiRenderer.cpp`:\n\n")
        for ov in sorted(overrides):
            f.write(f"- `renderer:{ov}(...)`\n")
        f.write("\n## Skipped Functions\n\n")
        f.write("| Function | Reason |\n|---|---|\n")
        for (n, reason) in skipped:
            f.write(f"| `{n}` | {reason} |\n")
        f.write("\n## Enums\n\n")
        f.write("Constants are accessible as `Imgui.Enum.<EnumName>.<Value>`, e.g.:\n\n")
        f.write("```lua\n")
        f.write("local flags = Imgui.Enum.ImGuiWindowFlags.NoTitleBar\n")
        f.write("local col   = Imgui.Enum.ImGuiCol.Button\n")
        f.write("```\n\n")
        f.write("| Lua path | C constant | Value |\n|---|---|---|\n")
        for lua_path, cname, value in enum_entries:
            f.write(f"| `{lua_path}` | `{cname}` | `{value}` |\n")

    print(f"Wrote {OUT_DOCS}")

    if skipped:
        print(f"\nSkipped {len(skipped)} functions:")
        for n, r in skipped:
            print(f"  {n}: {r}")


if __name__ == "__main__":
    main()
