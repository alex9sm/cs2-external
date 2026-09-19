#include <cstdint>
#include <cstdio>
#include <vector>
#include <optional>

#include "read_thread.h"

constexpr uint64_t OFF_LOCAL_PLAYER = 0x23CCC08;
constexpr uint64_t OFF_ENTITY_LIST = 0x2577BE0;
constexpr uint64_t OFF_VIEW_MATRIX = 0x23D21F0;
constexpr uint64_t OFF_TEAM = 0x3E7;
constexpr uint64_t OFF_LIFE_STATE = 0x354;
constexpr uint64_t OFF_PLAYER_PAWN = 0x914;
constexpr uint64_t OFF_V_OLD_ORIGIN = 0x13B8;
constexpr uint64_t OFF_HEALTH = 0x34C;

DWORD get_pid() {
    HWND handle = FindWindowA("SDL_app", "Counter-Strike 2");
    if (!handle) {
        puts("CS2 not running");
        return 0;
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(handle, &pid);
    return pid;
}

std::optional<HANDLE> get_handle(DWORD pid) {
    const auto ntdll = GetModuleHandleA("ntdll.dll");
    if (!ntdll) return {};

    using fn_query = long(__stdcall*)(unsigned long, void*, unsigned long, unsigned long*);
    using fn_dup = long(__stdcall*)(void*, void*, void*, void**, unsigned long, unsigned long, unsigned long);
    using fn_open = long(__stdcall*)(void**, unsigned long, OBJECT_ATTRIBUTES*, CLIENT_ID*);
    using fn_priv = long(__stdcall*)(unsigned long, unsigned char, unsigned char, unsigned char*);

    auto nt_query = reinterpret_cast<fn_query>(GetProcAddress(ntdll, "NtQuerySystemInformation"));
    auto nt_dup = reinterpret_cast<fn_dup>(GetProcAddress(ntdll, "NtDuplicateObject"));
    auto nt_open = reinterpret_cast<fn_open>(GetProcAddress(ntdll, "NtOpenProcess"));
    auto rtl_priv = reinterpret_cast<fn_priv>(GetProcAddress(ntdll, "RtlAdjustPrivilege"));

    if (!nt_query || !nt_dup || !nt_open || !rtl_priv) return {};

    uint8_t old = 0;
    rtl_priv(0x14, 1, 0, &old);
    OBJECT_ATTRIBUTES oa{};
    InitializeObjectAttributes(&oa, nullptr, 0, nullptr, nullptr);

    std::vector<uint8_t> buf(sizeof(sys_handle_info_t));
    unsigned long status;
    do {
        buf.resize(buf.size() * 2);
        status = nt_query(0x10, buf.data(), static_cast<unsigned long>(buf.size()), nullptr);
    }
    while (status == 0xc0000004);

    if (!NT_SUCCESS(status)) return {};

    auto* info = reinterpret_cast<sys_handle_info_t*>(buf.data());
    HANDLE owner_proc = nullptr;

    for (unsigned long i = 0; i < info->m_handle_count; i++) {
        const auto& h = info->m_handles[i];
        if (h.m_object_type_number != 0x07) continue; 
        if (reinterpret_cast<HANDLE>((uintptr_t)h.m_handle) == INVALID_HANDLE_VALUE) continue;

        CLIENT_ID cid{};
        cid.UniqueProcess = reinterpret_cast<HANDLE>((uintptr_t)h.m_process_id);
        if (owner_proc) {
            CloseHandle(owner_proc);
            owner_proc = nullptr;
        }

        if (!NT_SUCCESS(nt_open(&owner_proc, PROCESS_DUP_HANDLE, &oa, &cid))) continue;

        HANDLE dupe = nullptr;
        if (!NT_SUCCESS(nt_dup(owner_proc, reinterpret_cast<HANDLE>((uintptr_t)h.m_handle), GetCurrentProcess(),
                        reinterpret_cast<void**>(&dupe), PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, 0, 0))) {
            continue;
        }

        if (GetProcessId(dupe) == pid) {
            CloseHandle(owner_proc);
            return dupe;
        }

        CloseHandle(dupe);
    }

    if (owner_proc) CloseHandle(owner_proc);
    return {};
}

uint64_t get_module_base(DWORD pid, const wchar_t *mod_name) {
    uint64_t base_addr = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
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

void read_thread_func(SharedState& state) {
    DWORD pid = get_pid();
    if (!pid) return;

    auto game_handle = get_handle(pid);
    if (!game_handle.has_value()) {
        puts("get_handle failed, no handle found");
        return;
    }

    uint64_t base = get_module_base(pid, L"client.dll");
    if (!base) {
        CloseHandle(game_handle.value());
        return;
    }

    constexpr uint64_t PAWN_READ_START = OFF_HEALTH;
    constexpr size_t   PAWN_BUF_SIZE   = (OFF_V_OLD_ORIGIN + sizeof(vec3)) - PAWN_READ_START;

    std::vector<EnemyData> frame_enemies;
    uint8_t pawn_buf[PAWN_BUF_SIZE];

    while (state.running) {
        uint64_t entity_list = read<uint64_t>(game_handle.value(), base + OFF_ENTITY_LIST);
        uint64_t local_pawn = read<uint64_t>(game_handle.value(), base + OFF_LOCAL_PLAYER);
        view_matrix_t vm = read<view_matrix_t>(game_handle.value(), base + OFF_VIEW_MATRIX);

        frame_enemies.clear();

        if (entity_list && local_pawn) {
            int local_team = read<int>(game_handle.value(), local_pawn + OFF_TEAM);

            uint64_t chunk_cache[4] = {};
            bool     chunk_valid[4] = {};

            auto get_chunk = [&](uint32_t ci) -> uint64_t {
                if (ci >= 4) return 0;
                if (!chunk_valid[ci]) {
                    chunk_cache[ci] = read<uint64_t>(game_handle.value(), entity_list + 8 * ci + 16);
                    chunk_valid[ci] = true;
                }
                return chunk_cache[ci];
            };

            auto get_entity = [&](uint32_t idx) -> uint64_t {
                uint64_t chunk = get_chunk(idx >> 9);
                if (!chunk) return 0;
                return read<uint64_t>(game_handle.value(), chunk + 0x70 * (idx & 0x1FF));
            };

            for (int i = 1; i <= 64; i++) {
                uint64_t controller = get_entity(i);
                if (!controller) continue;

                uint32_t pawn_handle = read<uint32_t>(game_handle.value(), controller + OFF_PLAYER_PAWN);
                uint64_t pawn = get_entity(pawn_handle & 0x7FFF);
                if (!pawn || pawn == local_pawn) continue;

                if (!ReadProcessMemory(game_handle.value(), (LPCVOID)(pawn + PAWN_READ_START),
                                       pawn_buf, PAWN_BUF_SIZE, nullptr))
                    continue;

                int team = *(int*)(pawn_buf + OFF_TEAM - PAWN_READ_START);
                if (team == local_team) continue;

                uint8_t life = *(uint8_t*)(pawn_buf + OFF_LIFE_STATE - PAWN_READ_START);
                if (life != 0) continue;

                int hp = *(int*)(pawn_buf + OFF_HEALTH - PAWN_READ_START);
                if (hp <= 0) continue;

                vec3 feet = *(vec3*)(pawn_buf + OFF_V_OLD_ORIGIN - PAWN_READ_START);
                vec3 head = { feet.x, feet.y, feet.z + 72.0f };
                frame_enemies.push_back({ feet, head, hp });
            }
        }

        {
            std::lock_guard<std::mutex> lock(state.mtx);
            std::swap(state.enemies, frame_enemies);
            state.view_matrix = vm;
        }
    }

    CloseHandle(game_handle.value());
}
