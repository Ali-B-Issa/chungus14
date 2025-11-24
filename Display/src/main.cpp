#include "Display.h"
#include <iostream>
#include <csignal>

// Global display pointer for signal handling
Display* g_display = nullptr;

// Signal handler for shutdown (Ctrl+C)
void signalHandler(int signum) {
    std::cout << "\nDisplay: Received signal " << signum << ", shutting down...\n";
    if (g_display) {
        g_display->shutdown();
    }
}

int main() {
    
    // Set up signal handlers for  shutdown
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



