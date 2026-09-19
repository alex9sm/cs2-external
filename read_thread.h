#pragma once

#include <windows.h>
#include <winuser.h>
#include <tlhelp32.h>
#include <winternl.h>

#include "shared_data.h"

struct sys_handle_t {
    unsigned long  m_process_id;
    uint8_t        m_object_type_number;
    uint8_t        m_flags;
    unsigned short m_handle;
    void*          m_object;
    unsigned long  m_granted_access;
};

struct sys_handle_info_t {
    unsigned long   m_handle_count;
    sys_handle_t    m_handles[1];
};

DWORD get_pid();
uint64_t get_module_base(DWORD pid, const wchar_t *mod_name);

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

void read_thread_func(SharedState& state);
