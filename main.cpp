#include <cstdio>
#include <thread>

#include "read_thread.h"
#include "gui.h"

// cl /EHsc /std:c++17 /Ivendor/imgui main.cpp read_thread.cpp gui.cpp vendor/imgui/imgui.cpp vendor/imgui/imgui_draw.cpp vendor/imgui/imgui_tables.cpp vendor/imgui/imgui_widgets.cpp vendor/imgui/imgui_impl_win32.cpp vendor/imgui/imgui_impl_dx11.cpp /link user32.lib gdi32.lib

int main() {
    SharedState state;

    std::thread reader(read_thread_func, std::ref(state));

    if (!overlay::init()) {
        puts("init failed");
        state.running = false;
        reader.join();
        return 1;
    }

    overlay::run(state);

    state.running = false;
    reader.join();
    overlay::shutdown();
    return 0;
}
