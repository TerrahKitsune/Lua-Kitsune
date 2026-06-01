// Injected before duckdb.cpp via ForcedIncludeFiles -- do not include manually.
// Undefine the Windows SDK ''interface'' macro so DuckDB can use it as an identifier.
#ifdef interface
#undef interface
#endif
