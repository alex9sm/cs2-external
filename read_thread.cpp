#include <cstdint>
#include <cstdio>

#include "read_thread.h"

DWORD get_pid() {
    HWND handle = FindWindowA("SDL_app", "Counter-Strike 2");
    if (!handle) {
        puts("CS2 not running");
        return 0;
    }
    DWORD pid = 0;
    DWORD threadid = GetWindowThreadProcessId(handle, &pid);
    return pid;
}

uint64_t get_module_base(HANDLE hProc, DWORD pid, const wchar_t *mod_name) {
    uint64_t base_addr = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
    if (snapshot == INVALID_HANDLE_VALUE) {
        printf("CreateToolhelp32Snapshot failed: %lu\n", GetLastError());
        return 0;
    }

    MODULEENTRY32W me = { 0 };
    me.dwSize = sizeof(MODULEENTRY32W);
    if (Module32FirstW(snapshot, &me)) {
        do {
            if (_wcsicmp(me.szModule, mod_name) == 0) {
                base_addr = (uint64_t)me.modBaseAddr;
                break;
            }
        }
        while (Module32NextW(snapshot, &me));
    }
    else {
        printf("Module32FirstW failed: %lu\n", GetLastError());
        CloseHandle(snapshot);
        return 0;
    }
    CloseHandle(snapshot);
    return base_addr;
}

uint64_t get_entity(HANDLE h, uint64_t list, uint32_t idx) {
    uint64_t chunk = read<uint64_t>(h, list + 8 * (idx >> 9) + 16);
    if (!chunk) return 0;
    return read<uint64_t>(h, chunk + 0x78 * (idx & 0x1FF));
}

