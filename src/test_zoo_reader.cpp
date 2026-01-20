#define SDL_MAIN_HANDLED
#include "ZooReader.hpp"
#include "MemoryManager.hpp"

#include <SDL.h>
#include <iostream>

// Mock ResourceManager partial for testing? 
// Or just read file directly since we are testing ZooReader logic.

int main(int argc, char** argv) {
    SDL_LogSetAllPriority(SDL_LOG_PRIORITY_VERBOSE);
    SDL_Log("Starting ZooReader Test...");

    std::string path = "scenario/scn01/scn01.zoo";
    
    // Manual file read (simulate ResourceManager)
    // We already know how to read ztd/zoo from inspect_script.
    // For this test, lets just read "maps/large.zoo" if it exists, or try to read from ZTD?
    // This test gets complex if we need ZTD parsing.
    // Let's rely on the fact that we have "maps/large.zoo" (loose) or we can extract one.
    
    // Wait, inspect_zoo.py showed we have "maps/large.zoo" but it failed to open as ZIP?
    // Ah, inspect_zoo.py logic was: if .zoo extension, treat as file.
    // And it found TZFB in "maps/large.zoo".
    
    FILE* f = fopen("maps/large.zoo", "rb");
    if (!f) {
        // Try inside build dir if relative paths issue
        f = fopen("../../maps/large.zoo", "rb");
    }
    
    if (!f) {
        SDL_Log("Could not open maps/large.zoo for testing.");
        return 1;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    void* data = malloc(fsize);
    fread(data, 1, fsize, f);
    fclose(f);

    SDL_Log("Read %ld bytes from large.zoo", fsize);

    ZooReader reader;
    AssetBuffer buffer = MemoryManager::get().wrap(data, fsize);
    
    if (reader.load(buffer)) { // buffer moved here? No, load takes const ref? 
       // load(const AssetBuffer& buffer) -> copy? 
       // AssetBuffer has no copy ctor!
       // Signature: bool load(const AssetBuffer& buffer); -> This requires copy if passed by value, 
       // but wait, AssetBuffer(const AssetBuffer&) = delete; 
       // So I MUST pass by reference.
       
       SDL_Log("Map Width: %u", reader.getMapWidth());
       SDL_Log("Map Height: %u", reader.getMapHeight());
       
       if (reader.getMapWidth() > 0 && reader.getMapHeight() > 0) {
           SDL_Log("TEST PASSED");
           return 0;
       }
    } else {
        SDL_Log("ZooReader failed to load.");
    }

    return 1;
}
