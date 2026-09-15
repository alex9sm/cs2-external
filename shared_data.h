#pragma once

#include <vector>
#include <mutex>

struct vec2 { 
    float x, y; 
};
struct vec3 { 
    float x, y, z; 
};

struct view_matrix_t {
    float m[4][4];
};

struct EnemyData {
    vec3 feet;
    vec3 head;
    int health;
};

struct SharedState {
    std::mutex mtx;
    std::vector<EnemyData> enemies;
    view_matrix_t view_matrix{};
    bool running = true;
};
