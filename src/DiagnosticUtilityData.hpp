#ifndef DIAGNOSTIC_UTILITY_DATA_HPP
#define DIAGNOSTIC_UTILITY_DATA_HPP

#include <SDL2/SDL.h>
#include <SDL_ttf.h>
#include <string>
#include <vector>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#endif

class DiagnosticUtilityData {
public:
    DiagnosticUtilityData();
    ~DiagnosticUtilityData();

    void update();
    void draw(SDL_Renderer* renderer);
    void logToFile();

private:
    // Timing
    uint32_t last_tick;
    uint32_t frame_count;
    uint32_t last_stat_update;
    
    // Live Stats
    float current_fps;
    float frame_time_ms;
    size_t memory_usage_mb;
    double cpu_usage_percent;
    std::string renderer_info;
    std::string resolution_info;

    // Rendering
    TTF_Font* debug_font;
    
    // CPU Calculation State
    #ifdef _WIN32
    ULARGE_INTEGER last_sys_cpu;
    ULARGE_INTEGER last_proc_cpu;
    int num_processors;
    HANDLE self_handle;
    #endif

    // Helpers
    void initCPU();
    void updateSystemStats(SDL_Renderer* renderer);
    void renderText(SDL_Renderer* renderer, std::string text, int x, int y, SDL_Color color);
};

#endif // DIAGNOSTIC_UTILITY_DATA_HPP
