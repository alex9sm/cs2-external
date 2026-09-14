#pragma once

#include <windows.h>
#include <winuser.h>
#include <tlhelp32.h>

DWORD get_pid();
uint64_t get_module_base(HANDLE hProc, DWORD pid, const wchar_t *mod_name);

template <typename T>
bool rpm(HANDLE hProc, uint64_t addr, T* out) {
    return ReadProcessMemory(hProc, (LPCVOID)addr, out, sizeof(T), nullptr);
}

template <typename T>
T read(HANDLE h, uint64_t addr, bool *ok = nullptr) {
    T buffer{};
    bool success = rpm(h, addr, &buffer);
    if (ok) {
        *ok = success;
    }
    return success ? buffer : T{};
}

uint64_t get_entity(HANDLE h, uint64_t list, uint32_t idx);
