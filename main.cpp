#include <cstdio>
#include <cstring>
#include <iostream>

#include "read_thread.h"

constexpr uint64_t OFF_LOCAL_PLAYER = 0x23CCC08;
constexpr uint64_t OFF_ENTITY_LIST = 0x2577BE0;
constexpr uint64_t OFF_VIEW_MATRIX = 0x23D21F0;
constexpr uint64_t OFF_TEAM = 0x3E7;
constexpr uint64_t OFF_LIFE_STATE = 0x354;
constexpr uint64_t OFF_PLAYER_PAWN = 0x914;
constexpr uint64_t OFF_V_OLD_ORIGIN = 0x13B8;
constexpr uint64_t OFF_HEALTH = 0x34C;

struct vec3 { float x, y, z; };

int main() {
    DWORD pid = get_pid();

    HANDLE game_handle = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!game_handle) {
        puts("OpenProcess failed");
        return 1;
    }
    
    uint64_t process_base_addr = get_module_base(game_handle, pid, L"client.dll");

    while (true) {
        uint64_t entity_list = read<uint64_t>(game_handle, process_base_addr + OFF_ENTITY_LIST);
        uint64_t local_pawn = read<uint64_t>(game_handle, process_base_addr + OFF_LOCAL_PLAYER);
        if (entity_list && local_pawn) {
            int local_team = read<int>(game_handle, local_pawn + OFF_TEAM);
            for (int i = 1; i <= 64; i++) {
                uint64_t controller = get_entity(game_handle, entity_list, i);
                if (!controller) continue;

                int pawn_handle = read<uint32_t>(game_handle, controller + OFF_PLAYER_PAWN);
                uint64_t pawn = get_entity(game_handle, entity_list, pawn_handle &0x7FFF);
                if (!pawn || pawn == local_pawn) continue;

                if (read<int>(game_handle, pawn + OFF_TEAM)  == local_team) continue;
                if (read<uint8_t>(game_handle, pawn + OFF_LIFE_STATE) != 0) continue;
                if (read<int>(game_handle, pawn + OFF_HEALTH) <= 0) continue;

                vec3 origin = read<vec3>(game_handle, pawn + OFF_V_OLD_ORIGIN);
                printf("[%2u] enemy at pos %.1f %.1f %.1f\n", i, origin.x, origin.y, origin.z);
            }
        }
        Sleep(50);
    }

    CloseHandle(game_handle);

    return 0;
}