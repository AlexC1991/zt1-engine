#include "DiagnosticUtilityData.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#endif

DiagnosticUtilityData::DiagnosticUtilityData() {
    SDL_Log("--- [DIAG] Upgrading Diagnostic Utility ---");
    last_tick = SDL_GetTicks();
    frame_count = 0;
    last_stat_update = 0;
    
    current_fps = 0.0f;
    frame_time_ms = 0.0f;
    memory_usage_mb = 0;
    cpu_usage_percent = 0.0;
    renderer_info = "Unknown";
    resolution_info = "Unknown";

    debug_font = nullptr;

    // Font Loading Strategy
    if (!TTF_WasInit()) TTF_Init();
    
    const char* fonts[] = {
        "C:\\Windows\\Fonts\\consola.ttf", // Monospaced is best for stats
        "C:\\Windows\\Fonts\\arial.ttf",
        "ui/font.ttf"
    };

    for (const char* fontPath : fonts) {
        debug_font = TTF_OpenFont(fontPath, 14); // Slightly smaller for more data
        if (debug_font) break;
    }

    initCPU();
}

DiagnosticUtilityData::~DiagnosticUtilityData() {
    logToFile();
    if (debug_font) TTF_CloseFont(debug_font);
}

void DiagnosticUtilityData::initCPU() {
#ifdef _WIN32
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    num_processors = sysInfo.dwNumberOfProcessors;
    self_handle = GetCurrentProcess();

    FILETIME ftime, fsys, fuser;
    FILETIME sysIdle, sysKernel, sysUser;

    GetSystemTimeAsFileTime(&ftime);
    memcpy(&last_sys_cpu, &ftime, sizeof(FILETIME));

    GetProcessTimes(self_handle, &ftime, &ftime, &fsys, &fuser);
    memcpy(&last_proc_cpu, &fsys, sizeof(FILETIME));
    last_proc_cpu.QuadPart += ((ULARGE_INTEGER*)&fuser)->QuadPart;
#endif
}

void DiagnosticUtilityData::update() {
    uint32_t current_tick = SDL_GetTicks();
    frame_count++;

    // Update stats every 500ms for responsiveness
    if (current_tick - last_stat_update >= 500) {
        float interval = (current_tick - last_stat_update) / 1000.0f;
        current_fps = frame_count / interval;
        frame_time_ms = 1000.0f / (current_fps > 0 ? current_fps : 60.0f);
        
        frame_count = 0;
        last_stat_update = current_tick;
        
        // We pass NULL here because we can't easily get renderer in update()
        // We will grab renderer info inside draw() once instead
        updateSystemStats(nullptr);
    }
}

void DiagnosticUtilityData::updateSystemStats(SDL_Renderer* renderer) {
#ifdef _WIN32
    // 1. Memory
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        memory_usage_mb = pmc.WorkingSetSize / 1024 / 1024;
    }

    // 2. CPU Calculation
    FILETIME ftime, fsys, fuser;
    ULARGE_INTEGER now, sys, user;
    double percent = 0.0;

    GetSystemTimeAsFileTime(&ftime);
    memcpy(&now, &ftime, sizeof(FILETIME));

    GetProcessTimes(self_handle, &ftime, &ftime, &fsys, &fuser);
    memcpy(&sys, &fsys, sizeof(FILETIME));
    memcpy(&user, &fuser, sizeof(FILETIME));
    sys.QuadPart += user.QuadPart;

    if (num_processors > 0) {
        percent = (sys.QuadPart - last_proc_cpu.QuadPart) * 100.0 / 
                  (now.QuadPart - last_sys_cpu.QuadPart) / num_processors;
        
        // Simple smoothing
        cpu_usage_percent = (cpu_usage_percent * 0.7) + (percent * 0.3);
    }
    
    last_sys_cpu = now;
    last_proc_cpu = sys;
#endif

    // 3. Renderer Info (One time fetch)
    if (renderer && renderer_info == "Unknown") {
        SDL_RendererInfo info;
        if (SDL_GetRendererInfo(renderer, &info) == 0) {
            renderer_info = info.name;
        }
        
        int w, h;
        if (SDL_GetRendererOutputSize(renderer, &w, &h) == 0) {
            std::stringstream ss;
            ss << w << "x" << h;
            resolution_info = ss.str();
        }
    }
}

void DiagnosticUtilityData::draw(SDL_Renderer* renderer) {
    if (!renderer) return;
    
    // Ensure we have renderer info
    if (renderer_info == "Unknown") updateSystemStats(renderer);

    // RESET VIEWPORT (Fixes "Invisible Box" bug)
    SDL_RenderSetScale(renderer, 1.0f, 1.0f);
    SDL_RenderSetViewport(renderer, NULL);
    
    // Background Box (Darker, slightly transparent)
    SDL_Rect box = { 10, 10, 260, 110 }; // Taller for more data
    SDL_SetRenderDrawColor(renderer, 10, 10, 10, 220); // Nearly Black
    SDL_RenderFillRect(renderer, &box);
    
    // Border (Green if good FPS, Red if bad)
    if (current_fps < 30) SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
    else SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255);
    SDL_RenderDrawRect(renderer, &box);

    if (debug_font) {
        std::stringstream ss;
        SDL_Color white = {255, 255, 255, 255};
        SDL_Color gray = {200, 200, 200, 255};
        SDL_Color yellow = {255, 255, 0, 255};
        
        // Line 1: FPS & FrameTime
        ss << "FPS: " << std::fixed << std::setprecision(1) << current_fps 
           << " (" << std::setprecision(1) << frame_time_ms << "ms)";
        renderText(renderer, ss.str(), 20, 15, white);

        // Line 2: RAM
        ss.str("");
        ss << "RAM: " << memory_usage_mb << " MB";
        renderText(renderer, ss.str(), 20, 35, gray);
        
        // Line 3: CPU
        ss.str("");
        ss << "CPU: " << std::fixed << std::setprecision(1) << cpu_usage_percent << "%";
        renderText(renderer, ss.str(), 20, 55, gray);
        
        // Line 4: Renderer
        ss.str("");
        ss << "GPU: " << renderer_info;
        renderText(renderer, ss.str(), 20, 75, yellow);

        // Line 5: Res
        ss.str("");
        ss << "RES: " << resolution_info;
        renderText(renderer, ss.str(), 20, 95, gray);
    }
}

void DiagnosticUtilityData::renderText(SDL_Renderer* renderer, std::string text, int x, int y, SDL_Color color) {
    SDL_Surface* surface = TTF_RenderText_Solid(debug_font, text.c_str(), color);
    if (surface) {
        SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
        if (texture) {
            SDL_Rect dest = { x, y, surface->w, surface->h };
            SDL_RenderCopy(renderer, texture, NULL, &dest);
            SDL_DestroyTexture(texture);
        }
        SDL_FreeSurface(surface);
    }
}

void DiagnosticUtilityData::logToFile() {
    std::ofstream outfile("diagnostics_log.txt", std::ios::app);
    if (outfile.is_open()) {
        auto now = std::time(nullptr);
        outfile << "Session End: " << std::put_time(std::localtime(&now), "%Y-%m-%d %H:%M:%S") << "\n";
        outfile << "Final FPS: " << current_fps << "\n";
        outfile << "Peak RAM: " << memory_usage_mb << " MB\n";
        outfile << "Final CPU: " << cpu_usage_percent << " %\n";
        outfile << "Renderer: " << renderer_info << "\n";
        outfile << "--------------------------------\n";
        outfile.close();
    }
}
