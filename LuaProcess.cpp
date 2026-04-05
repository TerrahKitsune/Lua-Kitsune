#include "LuaProcess.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <Windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#endif

// ── Common ────────────────────────────────────────────────────────────────────

LuaProcess* lua_toprocess(lua_State* L, int index) {
	LuaProcess* proc = (LuaProcess*)lua_touserdata(L, index);
	if (proc == NULL)
		luaL_error(L, "parameter is not a %s", LUAPROCESS);
	return proc;
}

LuaProcess* lua_pushprocess(lua_State* L) {
	LuaProcess* proc = (LuaProcess*)lua_newuserdata(L, sizeof(LuaProcess));
	if (proc == NULL)
		luaL_error(L, "Unable to create processmeta");
	luaL_getmetatable(L, LUAPROCESS);
	lua_setmetatable(L, -2);
	memset(proc, 0, sizeof(LuaProcess));
#ifdef _WIN32
	proc->hChildStd_IN_Rd  = INVALID_HANDLE_VALUE;
	proc->hChildStd_IN_Wr  = INVALID_HANDLE_VALUE;
	proc->hChildStd_OUT_Rd = INVALID_HANDLE_VALUE;
	proc->hChildStd_OUT_Wr = INVALID_HANDLE_VALUE;
	proc->hChildStd_ERR_Rd = INVALID_HANDLE_VALUE;
	proc->hChildStd_ERR_Wr = INVALID_HANDLE_VALUE;
#else
	proc->fd_in_r  = -1;
	proc->fd_in_w  = -1;
	proc->fd_out_r = -1;
	proc->fd_out_w = -1;
	proc->fd_err_r = -1;
	proc->fd_err_w = -1;
#endif
	return proc;
}

int process_tostring(lua_State* L) {
	char tim[100];
	sprintf(tim, "Process: %p", (void*)lua_toprocess(L, 1));
	lua_pushfstring(L, tim);
	return 1;
}

// ── Windows ───────────────────────────────────────────────────────────────────
#ifdef _WIN32

static char procname[MAX_PATH];
const char* GetProcessName(int id) {
	HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION |
		PROCESS_VM_READ,
		FALSE, id);

	memset(procname, 0, sizeof(MAX_PATH));

	if (hProcess != NULL) {
		HMODULE hMod;
		DWORD cbNeeded;

		if (EnumProcessModules(hProcess, &hMod, sizeof(hMod), &cbNeeded))
			GetModuleBaseName(hProcess, hMod, procname, MAX_PATH);
	}

	CloseHandle(hProcess);
	procname[MAX_PATH - 1] = '\0';
	return procname;
}

int GetAllProcesses(lua_State* L) {
	DWORD processes[1024];
	DWORD needed;
	const char* name;
	if (!EnumProcesses(processes, sizeof(processes), &needed)) {
		lua_pushnil(L);
		return 1;
	}

	needed = needed / sizeof(DWORD);
	lua_createtable(L, 0, needed);
	for (unsigned int n = 0; n < needed; n++) {
		name = GetProcessName(processes[n]);
		lua_pushinteger(L, processes[n]);
		lua_pushstring(L, name);
		lua_settable(L, -3);
	}

	return 1;
}

int LuaOpenProcess(lua_State* L) {
	int processid = (int)luaL_optinteger(L, 1, 0);
	lua_pop(L, lua_gettop(L));
	HANDLE proc;
	if (processid == 0) {
		proc = GetCurrentProcess();
		processid = GetProcessId(proc);
	}
	else {
		proc = OpenProcess(PROCESS_ALL_ACCESS, false, processid);
	}

	if (proc) {
		LuaProcess* lproc = lua_pushprocess(L);
		lproc->processInfo.dwProcessId = processid;
		lproc->processInfo.hProcess = proc;

		SYSTEM_INFO sysInfo;
		FILETIME ftime, fsys, fuser;

		GetSystemInfo(&sysInfo);
		lproc->numProcessors = sysInfo.dwNumberOfProcessors;

		GetSystemTimeAsFileTime(&ftime);
		memcpy(&lproc->lastCPU, &ftime, sizeof(FILETIME));

		GetProcessTimes(lproc->processInfo.hProcess, &ftime, &ftime, &fsys, &fuser);
		memcpy(&lproc->lastSysCPU, &fsys, sizeof(FILETIME));
		memcpy(&lproc->lastUserCPU, &fuser, sizeof(FILETIME));
	}
	else {
		lua_pushnil(L);
	}

	return 1;
}

int StartNewProcess(lua_State* L) {
	const char* appname = lua_tostring(L, 1);
	const char* cmd = lua_tostring(L, 2);
	const char* dir = lua_tostring(L, 3);
	bool noconsole = lua_toboolean(L, 4) > 0;
	bool redirect = false;
	int mask = 0;

	if (!dir) {
		char defaultdir[MAX_PATH];
		defaultdir[GetCurrentDirectory(MAX_PATH, defaultdir)] = '\0';
		dir = defaultdir;
	}

	if (lua_gettop(L) >= 5) {
		if (lua_isboolean(L, 5)) {
			redirect = lua_toboolean(L, 5) > 0;
			mask = LUA_PROC_IN | LUA_PROC_OUT | LUA_PROC_ERR;
		}
		else {
			mask = lua_tointeger(L, 5) & (LUA_PROC_IN | LUA_PROC_OUT | LUA_PROC_ERR);
			redirect = mask > 0;
		}
	}

	DWORD flag = CREATE_NEW_CONSOLE | NORMAL_PRIORITY_CLASS;
	if (noconsole)
		flag = NORMAL_PRIORITY_CLASS;

	STARTUPINFO info;
	PROCESS_INFORMATION processInfo;
	HANDLE hChildStd_OUT_Rd = INVALID_HANDLE_VALUE;
	HANDLE hChildStd_OUT_Wr = INVALID_HANDLE_VALUE;
	HANDLE hChildStd_IN_Rd  = INVALID_HANDLE_VALUE;
	HANDLE hChildStd_IN_Wr  = INVALID_HANDLE_VALUE;
	HANDLE hChildStd_ERR_Rd = INVALID_HANDLE_VALUE;
	HANDLE hChildStd_ERR_Wr = INVALID_HANDLE_VALUE;

	ZeroMemory(&info, sizeof(STARTUPINFO));
	info.cb = sizeof(info);
	ZeroMemory(&processInfo, sizeof(PROCESS_INFORMATION));

	if (redirect) {
		SECURITY_ATTRIBUTES saAttr;
		saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
		saAttr.bInheritHandle = TRUE;
		saAttr.lpSecurityDescriptor = NULL;

		if (mask & LUA_PROC_OUT) {
			if (!CreatePipe(&hChildStd_OUT_Rd, &hChildStd_OUT_Wr, &saAttr, 0)) {
				lua_pop(L, lua_gettop(L));
				lua_pushnil(L);
				lua_pushfstring(L, "Unable to open process %d", GetLastError());
				return 2;
			}
			if (!SetHandleInformation(hChildStd_OUT_Rd, HANDLE_FLAG_INHERIT, 0)) {
				lua_pop(L, lua_gettop(L));
				lua_pushnil(L);
				lua_pushfstring(L, "Unable to open process %d", GetLastError());
				return 2;
			}
			info.hStdOutput = hChildStd_OUT_Wr;
		}

		if (mask & LUA_PROC_IN) {
			if (!CreatePipe(&hChildStd_IN_Rd, &hChildStd_IN_Wr, &saAttr, 0)) {
				lua_pop(L, lua_gettop(L));
				lua_pushnil(L);
				lua_pushfstring(L, "Unable to open process %d", GetLastError());
				return 2;
			}
			if (!SetHandleInformation(hChildStd_IN_Wr, HANDLE_FLAG_INHERIT, 0)) {
				lua_pop(L, lua_gettop(L));
				lua_pushnil(L);
				lua_pushfstring(L, "Unable to open process %d", GetLastError());
				return 2;
			}
			info.hStdInput = hChildStd_IN_Rd;
		}

		if (mask & LUA_PROC_ERR) {
			if (!CreatePipe(&hChildStd_ERR_Rd, &hChildStd_ERR_Wr, &saAttr, 0)) {
				lua_pop(L, lua_gettop(L));
				lua_pushnil(L);
				lua_pushfstring(L, "Unable to open process %d", GetLastError());
				return 2;
			}
			if (!SetHandleInformation(hChildStd_ERR_Rd, HANDLE_FLAG_INHERIT, 0)) {
				lua_pop(L, lua_gettop(L));
				lua_pushnil(L);
				lua_pushfstring(L, "Unable to open process %d", GetLastError());
				return 2;
			}
			info.hStdError = hChildStd_ERR_Wr;
		}

		info.dwFlags |= STARTF_USESTDHANDLES;
	}

	if (CreateProcess(appname, (LPSTR)cmd, NULL, NULL, redirect, flag, NULL, dir, &info, &processInfo)) {
		lua_pop(L, lua_gettop(L));
		LuaProcess* proc = lua_pushprocess(L);
		proc->info = info;
		proc->processInfo = processInfo;

		SYSTEM_INFO sysInfo;
		FILETIME ftime, fsys, fuser;

		GetSystemInfo(&sysInfo);
		proc->numProcessors = sysInfo.dwNumberOfProcessors;

		GetSystemTimeAsFileTime(&ftime);
		memcpy(&proc->lastCPU, &ftime, sizeof(FILETIME));

		GetProcessTimes(proc->processInfo.hProcess, &ftime, &ftime, &fsys, &fuser);
		memcpy(&proc->lastSysCPU, &fsys, sizeof(FILETIME));
		memcpy(&proc->lastUserCPU, &fuser, sizeof(FILETIME));

		proc->hChildStd_IN_Rd  = hChildStd_IN_Rd;
		proc->hChildStd_IN_Wr  = hChildStd_IN_Wr;
		proc->hChildStd_OUT_Rd = hChildStd_OUT_Rd;
		proc->hChildStd_OUT_Wr = hChildStd_OUT_Wr;
		proc->hChildStd_ERR_Rd = hChildStd_ERR_Rd;
		proc->hChildStd_ERR_Wr = hChildStd_ERR_Wr;

		return 1;
	}
	else {
		lua_pop(L, lua_gettop(L));
		lua_pushnil(L);
		lua_pushfstring(L, "Unable to open process %d", GetLastError());
		return 2;
	}
}

int WriteToPipe(lua_State* L) {
	LuaProcess* proc = lua_toprocess(L, 1);
	size_t len;
	const char* data = luaL_checklstring(L, 2, &len);
	DWORD written;
	if (proc->hChildStd_IN_Wr == INVALID_HANDLE_VALUE) {
		lua_pop(L, lua_gettop(L));
		lua_pushinteger(L, -1);
		return 1;
	}

	BOOL success = WriteFile(proc->hChildStd_IN_Wr, data, (DWORD)len, &written, NULL);

	if (!success) {
		lua_pop(L, lua_gettop(L));
		lua_pushinteger(L, -1);
		return 1;
	}

	FlushFileBuffers(proc->hChildStd_IN_Wr);

	lua_pop(L, lua_gettop(L));
	lua_pushinteger(L, written);
	return 1;
}

int ReadFromPipe(lua_State* L) {
	LuaProcess* proc = lua_toprocess(L, 1);
	unsigned int buffersize = (unsigned int)luaL_optinteger(L, 2, 1048576);

	if (proc->hChildStd_OUT_Rd == INVALID_HANDLE_VALUE) {
		lua_pop(L, lua_gettop(L));
		lua_pushnil(L);
		return 1;
	}

	if (buffersize <= 0)
		buffersize = 1;

	char* data = (char*)gff_malloc(buffersize);
	if (!data) {
		lua_pop(L, lua_gettop(L));
		lua_pushnil(L);
		return 1;
	}

	DWORD read = 1;
	BOOL success = PeekNamedPipe(proc->hChildStd_OUT_Rd, data, 1, NULL, &read, NULL);

	if (success) {
		if (read <= 0) {
			gff_free(data);
			lua_pop(L, lua_gettop(L));
			lua_pushnil(L);
			return 1;
		}
		success = ReadFile(proc->hChildStd_OUT_Rd, data, buffersize - 1, &read, NULL);
		data[read] = '\0';
	}

	if (!success) {
		gff_free(data);
		lua_pop(L, lua_gettop(L));
		lua_pushnil(L);
		return 1;
	}

	lua_pop(L, lua_gettop(L));
	lua_pushlstring(L, data, read);
	gff_free(data);
	return 1;
}

int ErrorFromPipe(lua_State* L) {
	LuaProcess* proc = lua_toprocess(L, 1);
	unsigned int buffersize = (unsigned int)luaL_optinteger(L, 2, 1048576);

	if (proc->hChildStd_ERR_Rd == INVALID_HANDLE_VALUE) {
		lua_pop(L, lua_gettop(L));
		lua_pushnil(L);
		return 1;
	}

	if (buffersize <= 0)
		buffersize = 1;

	char* data = (char*)gff_malloc(buffersize);
	if (!data) {
		lua_pop(L, lua_gettop(L));
		lua_pushnil(L);
		return 1;
	}

	DWORD read = 1;
	BOOL success = PeekNamedPipe(proc->hChildStd_ERR_Rd, data, 1, NULL, &read, NULL);

	if (success) {
		if (read <= 0) {
			gff_free(data);
			lua_pop(L, lua_gettop(L));
			lua_pushnil(L);
			return 1;
		}
		success = ReadFile(proc->hChildStd_ERR_Rd, data, buffersize - 1, &read, NULL);
		data[read] = '\0';
	}

	if (!success) {
		gff_free(data);
		lua_pop(L, lua_gettop(L));
		lua_pushnil(L);
		return 1;
	}

	lua_pop(L, lua_gettop(L));
	lua_pushlstring(L, data, read);
	gff_free(data);
	return 1;
}

int GetSetPriority(lua_State* L) {
	LuaProcess* proc = lua_toprocess(L, 1);
	DWORD prio = GetPriorityClass(proc->processInfo.hProcess);

	if (lua_type(L, 2) == LUA_TNUMBER) {
		prio = (DWORD)SetPriorityClass(proc->processInfo.hProcess, (DWORD)lua_tointeger(L, 2));
		lua_pop(L, lua_gettop(L));
		lua_pushboolean(L, prio > 0);
	}
	else {
		lua_pop(L, lua_gettop(L));
		lua_pushinteger(L, prio > 0);
	}

	return 1;
}

int GetThreads(lua_State* L) {
	LuaProcess* proc = lua_toprocess(L, 1);

	HANDLE hThreadSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	THREADENTRY32 te32;

	if (hThreadSnap == INVALID_HANDLE_VALUE) {
		lua_pop(L, lua_gettop(L));
		lua_pushnil(L);
		lua_pushstring(L, "Unable to retrive snapshot");
		return 2;
	}

	te32.dwSize = sizeof(THREADENTRY32);

	if (!Thread32First(hThreadSnap, &te32)) {
		CloseHandle(hThreadSnap);
		lua_pop(L, lua_gettop(L));
		lua_pushnil(L);
		lua_pushstring(L, "Unable to retrive any threads");
		return 2;
	}

	lua_pop(L, lua_gettop(L));
	lua_newtable(L);
	int n = 0;
	do {
		if (te32.th32OwnerProcessID == proc->processInfo.dwProcessId) {
			lua_createtable(L, 0, 3);

			lua_pushstring(L, "ID");
			lua_pushinteger(L, te32.th32ThreadID);
			lua_settable(L, -3);

			lua_pushstring(L, "BasePrio");
			lua_pushinteger(L, te32.tpBasePri);
			lua_settable(L, -3);

			lua_pushstring(L, "DeltaPrio");
			lua_pushinteger(L, te32.tpDeltaPri);
			lua_settable(L, -3);

			lua_rawseti(L, -2, ++n);
		}
	} while (Thread32Next(hThreadSnap, &te32));

	CloseHandle(hThreadSnap);
	return 1;
}

int GetSetAffinity(lua_State* L) {
	LuaProcess* proc = lua_toprocess(L, 1);
	DWORD64 newmask;
	DWORD64 process, system;

	bool ok = GetProcessAffinityMask(proc->processInfo.hProcess, &process, &system) > 0;

	if (!ok) {
		lua_pop(L, lua_gettop(L));
		lua_pushnil(L);
		lua_pushstring(L, "Unable to retrive process affinity mask");
		return 1;
	}

	if (lua_isnumber(L, 2)) {
		newmask = (DWORD)lua_tointeger(L, 2);
		ok = SetProcessAffinityMask(proc->processInfo.hProcess, newmask) > 0;

		if (!ok) {
			lua_pop(L, lua_gettop(L));
			lua_pushnil(L);
			lua_pushstring(L, "Unable to set process affinity mask");
			return 1;
		}
	}

	lua_pop(L, lua_gettop(L));
	lua_pushinteger(L, process);
	lua_pushinteger(L, system);
	return 2;
}

int GetProcId(lua_State* L) {
	LuaProcess* proc = lua_toprocess(L, 1);
	DWORD id = proc->processInfo.dwProcessId;
	lua_pop(L, 1);
	lua_pushinteger(L, id);
	return 1;
}

int GetProcName(lua_State* L) {
	LuaProcess* proc = lua_toprocess(L, 1);
	DWORD id = proc->processInfo.dwProcessId;
	lua_pop(L, 1);
	lua_pushstring(L, GetProcessName(id));
	return 1;
}

static double getCurrentValue(LuaProcess* proc) {
	FILETIME ftime, fsys, fuser;
	ULARGE_INTEGER now, sys, user;
	double percent;

	GetSystemTimeAsFileTime(&ftime);
	memcpy(&now, &ftime, sizeof(FILETIME));

	GetProcessTimes(proc->processInfo.hProcess, &ftime, &ftime, &fsys, &fuser);
	memcpy(&sys, &fsys, sizeof(FILETIME));
	memcpy(&user, &fuser, sizeof(FILETIME));
	percent = (double)(sys.QuadPart - proc->lastSysCPU.QuadPart) +
		(user.QuadPart - proc->lastUserCPU.QuadPart);
	percent /= (now.QuadPart - proc->lastCPU.QuadPart);
	percent /= proc->numProcessors;
	proc->lastCPU = now;
	proc->lastUserCPU = user;
	proc->lastSysCPU = sys;
	return percent * 100;
}

int GetCPU(lua_State* L) {
	LuaProcess* proc = lua_toprocess(L, 1);
	lua_pop(L, lua_gettop(L));
	lua_pushnumber(L, getCurrentValue(proc));
	return 1;
}

int GetMemory(lua_State* L) {
	LuaProcess* proc = lua_toprocess(L, 1);
	PROCESS_MEMORY_COUNTERS pmc;
	GetProcessMemoryInfo(proc->processInfo.hProcess, (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc));
	lua_pop(L, lua_gettop(L));
	lua_pushnumber(L, (lua_Number)pmc.WorkingSetSize);
	return 1;
}

int StopProcess(lua_State* L) {
	LuaProcess* proc = lua_toprocess(L, 1);
	if (TerminateProcess(proc->processInfo.hProcess, (UINT)lua_tointeger(L, 2))) {
		lua_pop(L, lua_gettop(L));
		lua_pushboolean(L, true);
		return 1;
	}
	lua_pop(L, lua_gettop(L));
	lua_pushboolean(L, false);
	return 1;
}

int GetExitCode(lua_State* L) {
	LuaProcess* proc = lua_toprocess(L, 1);
	lua_pop(L, 1);
	DWORD lpExitCode;
	if (GetExitCodeProcess(proc->processInfo.hProcess, &lpExitCode)) {
		if (lpExitCode == STILL_ACTIVE)
			lua_pushnil(L);
		else
			lua_pushinteger(L, lpExitCode);
	}
	else {
		lua_pushnil(L);
	}
	return 1;
}

int process_gc(lua_State* L) {
	LuaProcess* proc = lua_toprocess(L, 1);
	if (proc->processInfo.hProcess)
		CloseHandle(proc->processInfo.hProcess);
	if (proc->processInfo.hThread)
		CloseHandle(proc->processInfo.hThread);

	if (proc->hChildStd_IN_Rd  != INVALID_HANDLE_VALUE) CloseHandle(proc->hChildStd_IN_Rd);
	if (proc->hChildStd_IN_Wr  != INVALID_HANDLE_VALUE) CloseHandle(proc->hChildStd_IN_Wr);
	if (proc->hChildStd_OUT_Rd != INVALID_HANDLE_VALUE) CloseHandle(proc->hChildStd_OUT_Rd);
	if (proc->hChildStd_OUT_Wr != INVALID_HANDLE_VALUE) CloseHandle(proc->hChildStd_OUT_Wr);
	if (proc->hChildStd_ERR_Rd != INVALID_HANDLE_VALUE) CloseHandle(proc->hChildStd_ERR_Rd);
	if (proc->hChildStd_ERR_Wr != INVALID_HANDLE_VALUE) CloseHandle(proc->hChildStd_ERR_Wr);

	lua_pop(L, 1);
	return 0;
}

// ── Linux / POSIX ─────────────────────────────────────────────────────────────
#else

int GetAllProcesses(lua_State* L) {
	DIR* dir = opendir("/proc");
	if (!dir) {
		lua_pushnil(L);
		return 1;
	}
	lua_newtable(L);
	struct dirent* entry;
	while ((entry = readdir(dir)) != NULL) {
		// Only numeric directory names are PIDs
		const char* dname = entry->d_name;
		bool is_pid = dname[0] != '\0';
		for (int i = 0; dname[i] && is_pid; i++) {
			if (dname[i] < '0' || dname[i] > '9')
				is_pid = false;
		}
		if (!is_pid)
			continue;
		int pid = atoi(dname);
		if (pid <= 0)
			continue;
		char path[64];
		char procname[256] = { 0 };
		snprintf(path, sizeof(path), "/proc/%d/comm", pid);
		FILE* f = fopen(path, "r");
		if (f) {
			if (fgets(procname, sizeof(procname), f)) {
				size_t len = strlen(procname);
				if (len > 0 && procname[len - 1] == '\n')
					procname[len - 1] = '\0';
			}
			fclose(f);
		}
		lua_pushinteger(L, pid);
		lua_pushstring(L, procname);
		lua_settable(L, -3);
	}
	closedir(dir);
	return 1;
}

int LuaOpenProcess(lua_State* L) {
	pid_t pid = (pid_t)luaL_optinteger(L, 1, 0);
	lua_pop(L, lua_gettop(L));
	if (pid == 0)
		pid = getpid();
	// kill(pid, 0) probes existence without sending a signal
	if (pid > 0 && kill(pid, 0) != 0) {
		lua_pushnil(L);
		return 1;
	}
	LuaProcess* proc = lua_pushprocess(L);
	proc->pid = pid;
	return 1;
}

int StartNewProcess(lua_State* L) {
	const char* appname = lua_tostring(L, 1);
	const char* cmd     = lua_tostring(L, 2);
	const char* dir     = lua_tostring(L, 3);
	// arg4 (noconsole) is Windows-only, ignored on Linux
	int mask = 0;
	if (lua_gettop(L) >= 5) {
		if (lua_isboolean(L, 5))
			mask = lua_toboolean(L, 5) ? (LUA_PROC_IN | LUA_PROC_OUT | LUA_PROC_ERR) : 0;
		else
			mask = (int)lua_tointeger(L, 5) & (LUA_PROC_IN | LUA_PROC_OUT | LUA_PROC_ERR);
	}

	// Use cmd if available, fall back to appname (matches Windows CreateProcess semantics)
	const char* exec_cmd = cmd ? cmd : appname;
	if (!exec_cmd) {
		lua_pop(L, lua_gettop(L));
		lua_pushnil(L);
		lua_pushstring(L, "no command specified");
		return 2;
	}

	// If a working directory is requested, wrap the command in a subshell that
	// first changes to that directory.  posix_spawn has no portable chdir action
	// so we prepend "cd '<dir>' && " to the shell command string.
	char cd_buf[4096];
	if (dir && dir[0]) {
		snprintf(cd_buf, sizeof(cd_buf), "cd '%s' && %s", dir, exec_cmd);
		exec_cmd = cd_buf;
	}

	int fd_in[2]  = { -1, -1 };
	int fd_out[2] = { -1, -1 };
	int fd_err[2] = { -1, -1 };
	bool pipe_ok = true;

	if (pipe_ok && (mask & LUA_PROC_IN)  && pipe(fd_in)  != 0) pipe_ok = false;
	if (pipe_ok && (mask & LUA_PROC_OUT) && pipe(fd_out) != 0) pipe_ok = false;
	if (pipe_ok && (mask & LUA_PROC_ERR) && pipe(fd_err) != 0) pipe_ok = false;

	if (!pipe_ok) {
		if (fd_in[0]  >= 0) close(fd_in[0]);
		if (fd_in[1]  >= 0) close(fd_in[1]);
		if (fd_out[0] >= 0) close(fd_out[0]);
		if (fd_out[1] >= 0) close(fd_out[1]);
		if (fd_err[0] >= 0) close(fd_err[0]);
		if (fd_err[1] >= 0) close(fd_err[1]);
		lua_pop(L, lua_gettop(L));
		lua_pushnil(L);
		lua_pushstring(L, "failed to create process pipes");
		return 2;
	}

	// posix_spawn is safe to call from a multithreaded process (unlike fork+exec).
	// File actions are applied in the child before exec: dup2 the pipe ends onto
	// stdio, then close the original descriptors so the child has no spare fds.
	posix_spawn_file_actions_t fa;
	posix_spawn_file_actions_init(&fa);

	if (mask & LUA_PROC_IN) {
		posix_spawn_file_actions_adddup2(&fa, fd_in[0],  STDIN_FILENO);
		posix_spawn_file_actions_addclose(&fa, fd_in[0]);
		posix_spawn_file_actions_addclose(&fa, fd_in[1]);
	}
	if (mask & LUA_PROC_OUT) {
		posix_spawn_file_actions_adddup2(&fa, fd_out[1], STDOUT_FILENO);
		posix_spawn_file_actions_addclose(&fa, fd_out[0]);
		posix_spawn_file_actions_addclose(&fa, fd_out[1]);
	}
	if (mask & LUA_PROC_ERR) {
		posix_spawn_file_actions_adddup2(&fa, fd_err[1], STDERR_FILENO);
		posix_spawn_file_actions_addclose(&fa, fd_err[0]);
		posix_spawn_file_actions_addclose(&fa, fd_err[1]);
	}

	extern char** environ;
	const char* argv[] = { "sh", "-c", exec_cmd, NULL };
	pid_t pid = -1;
	int rc = posix_spawn(&pid, "/bin/sh", &fa, NULL, (char* const*)argv, environ);

	posix_spawn_file_actions_destroy(&fa);

	// Parent: close the child-side ends we no longer need
	if (mask & LUA_PROC_IN)  close(fd_in[0]);
	if (mask & LUA_PROC_OUT) close(fd_out[1]);
	if (mask & LUA_PROC_ERR) close(fd_err[1]);

	if (rc != 0) {
		if (mask & LUA_PROC_IN)  close(fd_in[1]);
		if (mask & LUA_PROC_OUT) close(fd_out[0]);
		if (mask & LUA_PROC_ERR) close(fd_err[0]);
		lua_pop(L, lua_gettop(L));
		lua_pushnil(L);
		lua_pushstring(L, "posix_spawn failed");
		return 2;
	}

	lua_pop(L, lua_gettop(L));
	LuaProcess* proc = lua_pushprocess(L);
	proc->pid      = pid;
	proc->fd_in_w  = (mask & LUA_PROC_IN)  ? fd_in[1]  : -1;
	proc->fd_out_r = (mask & LUA_PROC_OUT) ? fd_out[0] : -1;
	proc->fd_err_r = (mask & LUA_PROC_ERR) ? fd_err[0] : -1;
	return 1;
}

int WriteToPipe(lua_State* L) {
	LuaProcess* proc = lua_toprocess(L, 1);
	size_t len;
	const char* data = luaL_checklstring(L, 2, &len);
	if (proc->fd_in_w < 0) {
		lua_pop(L, lua_gettop(L));
		lua_pushinteger(L, -1);
		return 1;
	}
	ssize_t written = write(proc->fd_in_w, data, len);
	lua_pop(L, lua_gettop(L));
	lua_pushinteger(L, (lua_Integer)written);
	return 1;
}

// Non-blocking read helper: returns -1 if no data available, otherwise bytes read.
static ssize_t read_nonblocking(int fd, char* buf, size_t bufsize) {
	struct pollfd pfd;
	pfd.fd      = fd;
	pfd.events  = POLLIN;
	pfd.revents = 0;
	if (poll(&pfd, 1, 0) <= 0 || !(pfd.revents & POLLIN))
		return -1;
	return read(fd, buf, bufsize);
}

int ReadFromPipe(lua_State* L) {
	LuaProcess* proc = lua_toprocess(L, 1);
	unsigned int buffersize = (unsigned int)luaL_optinteger(L, 2, 1048576);
	if (proc->fd_out_r < 0) {
		lua_pop(L, lua_gettop(L));
		lua_pushnil(L);
		return 1;
	}
	if (buffersize < 1)
		buffersize = 1;
	char* data = (char*)gff_malloc(buffersize);
	if (!data) {
		lua_pop(L, lua_gettop(L));
		lua_pushnil(L);
		return 1;
	}
	ssize_t nread = read_nonblocking(proc->fd_out_r, data, buffersize - 1);
	if (nread <= 0) {
		gff_free(data);
		lua_pop(L, lua_gettop(L));
		lua_pushnil(L);
		return 1;
	}
	lua_pop(L, lua_gettop(L));
	lua_pushlstring(L, data, (size_t)nread);
	gff_free(data);
	return 1;
}

int ErrorFromPipe(lua_State* L) {
	LuaProcess* proc = lua_toprocess(L, 1);
	unsigned int buffersize = (unsigned int)luaL_optinteger(L, 2, 1048576);
	if (proc->fd_err_r < 0) {
		lua_pop(L, lua_gettop(L));
		lua_pushnil(L);
		return 1;
	}
	if (buffersize < 1)
		buffersize = 1;
	char* data = (char*)gff_malloc(buffersize);
	if (!data) {
		lua_pop(L, lua_gettop(L));
		lua_pushnil(L);
		return 1;
	}
	ssize_t nread = read_nonblocking(proc->fd_err_r, data, buffersize - 1);
	if (nread <= 0) {
		gff_free(data);
		lua_pop(L, lua_gettop(L));
		lua_pushnil(L);
		return 1;
	}
	lua_pop(L, lua_gettop(L));
	lua_pushlstring(L, data, (size_t)nread);
	gff_free(data);
	return 1;
}

int StopProcess(lua_State* L) {
	LuaProcess* proc = lua_toprocess(L, 1);
	lua_pop(L, lua_gettop(L));
	if (proc->pid <= 0) {
		lua_pushboolean(L, 0);
		return 1;
	}
	lua_pushboolean(L, kill(proc->pid, SIGTERM) == 0 ? 1 : 0);
	return 1;
}

int GetExitCode(lua_State* L) {
	LuaProcess* proc = lua_toprocess(L, 1);
	lua_pop(L, 1);
	if (proc->has_exited) {
		lua_pushinteger(L, proc->exit_status);
		return 1;
	}
	if (proc->pid <= 0) {
		lua_pushnil(L);
		return 1;
	}
	int status;
	pid_t result = waitpid(proc->pid, &status, WNOHANG);
	if (result == proc->pid) {
		proc->exit_status = WIFEXITED(status) ? (int)WEXITSTATUS(status) : -(int)WTERMSIG(status);
		proc->has_exited  = 1;
		lua_pushinteger(L, proc->exit_status);
	}
	else {
		lua_pushnil(L);  // still running
	}
	return 1;
}

int GetProcId(lua_State* L) {
	LuaProcess* proc = lua_toprocess(L, 1);
	pid_t id = proc->pid;
	lua_pop(L, 1);
	lua_pushinteger(L, (lua_Integer)id);
	return 1;
}

int GetProcName(lua_State* L) {
	LuaProcess* proc = lua_toprocess(L, 1);
	pid_t id = proc->pid;
	lua_pop(L, 1);
	if (id <= 0) {
		lua_pushnil(L);
		return 1;
	}
	char path[64];
	char name[256];
	snprintf(path, sizeof(path), "/proc/%d/comm", (int)id);
	FILE* f = fopen(path, "r");
	if (!f) {
		lua_pushnil(L);
		return 1;
	}
	if (!fgets(name, sizeof(name), f)) {
		fclose(f);
		lua_pushnil(L);
		return 1;
	}
	fclose(f);
	size_t len = strlen(name);
	if (len > 0 && name[len - 1] == '\n')
		name[len - 1] = '\0';
	lua_pushstring(L, name);
	return 1;
}

int GetMemory(lua_State* L) {
	LuaProcess* proc = lua_toprocess(L, 1);
	pid_t id = proc->pid;
	lua_pop(L, lua_gettop(L));
	if (id <= 0) {
		lua_pushnumber(L, 0);
		return 1;
	}
	char path[64];
	snprintf(path, sizeof(path), "/proc/%d/status", (int)id);
	FILE* f = fopen(path, "r");
	if (!f) {
		lua_pushnumber(L, 0);
		return 1;
	}
	char line[256];
	size_t vmrss_kb = 0;
	while (fgets(line, sizeof(line), f)) {
		if (strncmp(line, "VmRSS:", 6) == 0) {
			sscanf(line + 6, "%zu", &vmrss_kb);
			break;
		}
	}
	fclose(f);
	lua_pushnumber(L, (lua_Number)(vmrss_kb * 1024));
	return 1;
}

int process_gc(lua_State* L) {
	LuaProcess* proc = lua_toprocess(L, 1);
	if (proc->fd_in_r  >= 0) close(proc->fd_in_r);
	if (proc->fd_in_w  >= 0) close(proc->fd_in_w);
	if (proc->fd_out_r >= 0) close(proc->fd_out_r);
	if (proc->fd_out_w >= 0) close(proc->fd_out_w);
	if (proc->fd_err_r >= 0) close(proc->fd_err_r);
	if (proc->fd_err_w >= 0) close(proc->fd_err_w);
	// Reap the child without blocking if it has already exited
	if (proc->pid > 0 && !proc->has_exited) {
		int status;
		waitpid(proc->pid, &status, WNOHANG);
	}
	lua_pop(L, 1);
	return 0;
}

#endif // _WIN32
