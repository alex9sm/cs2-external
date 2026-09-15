#include <cstdint>
#include <cstdio>
#include <vector>

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

void read_thread_func(SharedState& state) {
    DWORD pid = get_pid();
    if (!pid) return;

    HANDLE game_handle = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!game_handle) {
        puts("OpenProcess failed");
        return;
    }

    uint64_t base = get_module_base(game_handle, pid, L"client.dll");
    if (!base) {
        CloseHandle(game_handle);
        return;
    }

    constexpr uint64_t PAWN_READ_START = OFF_HEALTH;
    constexpr size_t   PAWN_BUF_SIZE   = (OFF_V_OLD_ORIGIN + sizeof(vec3)) - PAWN_READ_START;

    std::vector<EnemyData> frame_enemies;
    uint8_t pawn_buf[PAWN_BUF_SIZE];

    while (state.running) {
        uint64_t entity_list = read<uint64_t>(game_handle, base + OFF_ENTITY_LIST);
        uint64_t local_pawn = read<uint64_t>(game_handle, base + OFF_LOCAL_PLAYER);
        view_matrix_t vm = read<view_matrix_t>(game_handle, base + OFF_VIEW_MATRIX);

        frame_enemies.clear();

        if (entity_list && local_pawn) {
            int local_team = read<int>(game_handle, local_pawn + OFF_TEAM);

            uint64_t chunk_cache[4] = {};
            bool     chunk_valid[4] = {};

            auto get_chunk = [&](uint32_t ci) -> uint64_t {
                if (ci >= 4) return 0;
                if (!chunk_valid[ci]) {
                    chunk_cache[ci] = read<uint64_t>(game_handle, entity_list + 8 * ci + 16);
                    chunk_valid[ci] = true;
                }
                return chunk_cache[ci];
            };

            auto get_entity = [&](uint32_t idx) -> uint64_t {
                uint64_t chunk = get_chunk(idx >> 9);
                if (!chunk) return 0;
                return read<uint64_t>(game_handle, chunk + 0x70 * (idx & 0x1FF));
            };

            for (int i = 1; i <= 64; i++) {
                uint64_t controller = get_entity(i);
                if (!controller) continue;

                uint32_t pawn_handle = read<uint32_t>(game_handle, controller + OFF_PLAYER_PAWN);
                uint64_t pawn = get_entity(pawn_handle & 0x7FFF);
                if (!pawn || pawn == local_pawn) continue;

                if (!ReadProcessMemory(game_handle, (LPCVOID)(pawn + PAWN_READ_START),
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

        Sleep(50);
    }

    CloseHandle(game_handle);
}
