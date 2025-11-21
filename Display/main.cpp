/*
 * main.cpp - Display System
 * 
 * ATC Display System for COEN320 Project
 * Group: AH_40247851_40228573
 * 
 * This is a separate QNX project that runs independently from
 * the Radar and Computer System processes.
 * 
 * Functions:
 * 1. Reads aircraft data from shared memory (written by Radar)
 * 2. Displays aircraft positions in a text-based grid
 * 3. Receives collision warnings from Computer System via IPC
 * 4. Highlights aircraft involved in potential collisions
 */

#include "Display.h"
#include <iostream>
#include <csignal>

// Global display pointer for signal handling
Display* g_display = nullptr;

// Signal handler for clean shutdown (Ctrl+C)
void signalHandler(int signum) {
    std::cout << "\nDisplay: Received signal " << signum << ", shutting down...\n";
    if (g_display) {
        g_display->shutdown();
    }
}

int main() {
    std::cout << "╔════════════════════════════════════════════╗\n";
    std::cout << "║      ATC Display System Starting...        ║\n";
    std::cout << "║      Group: AH_40247851_40228573           ║\n";
    std::cout << "║      COEN320 - Lab 4/5                     ║\n";
    std::cout << "╚════════════════════════════════════════════╝\n\n";

    // Set up signal handlers for clean shutdown
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    // Create Display instance
    Display display;
    g_display = &display;

    // Initialize the display system
    if (!display.initialize()) {
        std::cerr << "Display: Failed to initialize. Exiting.\n";
        return EXIT_FAILURE;
    }

    std::cout << "Display: Initialization complete. Starting display...\n\n";

    // Run the display (blocks until shutdown)
    display.run();

    std::cout << "\n╔════════════════════════════════════════════╗\n";
    std::cout << "║      Display System Shutdown Complete      ║\n";
    std::cout << "╚════════════════════════════════════════════╝\n";
    
    return EXIT_SUCCESS;
}
