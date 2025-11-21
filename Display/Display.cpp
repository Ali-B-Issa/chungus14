/*
 * Display.cpp
 * 
 * Display System for ATC Project
 * Group: AH_40247851_40228573
 */

#include "Display.h"
#include "ATCTimer.h"
#include <iomanip>
#include <sstream>
#include <cstring>

Display::Display() : shm_fd(-1), shared_mem(nullptr), display_channel(nullptr), running(false) {}

Display::~Display() {
    shutdown();
}

bool Display::initialize() {
    if (!initializeSharedMemory()) {
        std::cerr << "Display: Failed to initialize shared memory\n";
        return false;
    }
    if (!initializeIPCChannel()) {
        std::cerr << "Display: Failed to initialize IPC channel\n";
        cleanupSharedMemory();
        return false;
    }
    running = true;
    return true;
}

bool Display::initializeSharedMemory() {
    // Keep trying until shared memory is available
    while (true) {
        shm_fd = shm_open(SHARED_MEMORY_NAME, O_RDONLY, 0666);
        if (shm_fd == -1) {
            std::cout << "Display: Waiting for shared memory...\n";
            sleep(1);
            continue;
        }

        shared_mem = (SharedMemory*)mmap(NULL, sizeof(SharedMemory), 
                                          PROT_READ, MAP_SHARED, shm_fd, 0);
        if (shared_mem == MAP_FAILED) {
            std::cerr << "Display: Failed to map shared memory\n";
            close(shm_fd);
            shm_fd = -1;
            sleep(1);
            continue;
        }

        std::cout << "Display: Shared memory initialized successfully\n";
        return true;
    }
}

bool Display::initializeIPCChannel() {
    display_channel = name_attach(NULL, DISPLAY_CHANNEL_NAME, 0);
    if (display_channel == NULL) {
        std::cerr << "Display: Failed to create channel: " << DISPLAY_CHANNEL_NAME << "\n";
        std::cerr << "  Error: " << strerror(errno) << "\n";
        return false;
    }
    std::cout << "Display: IPC channel created: " << DISPLAY_CHANNEL_NAME << "\n";
    return true;
}

void Display::cleanupSharedMemory() {
    if (shared_mem && shared_mem != MAP_FAILED) {
        munmap(shared_mem, sizeof(SharedMemory));
        shared_mem = nullptr;
    }
    if (shm_fd != -1) {
        close(shm_fd);
        shm_fd = -1;
    }
}

void Display::cleanupIPCChannel() {
    if (display_channel) {
        name_detach(display_channel, 0);
        display_channel = nullptr;
    }
}

void Display::shutdown() {
    running = false;
    
    if (displayThread.joinable()) {
        displayThread.join();
    }
    if (collisionListenerThread.joinable()) {
        collisionListenerThread.join();
    }
    
    cleanupIPCChannel();
    cleanupSharedMemory();
    
    std::cout << "Display: Shutdown complete\n";
}

void Display::run() {
    // Start the display thread
    displayThread = std::thread(&Display::displayAircraft, this);
    
    // Start the collision listener thread
    collisionListenerThread = std::thread(&Display::listenForCollisions, this);
    
    // Wait for threads to complete
    if (displayThread.joinable()) {
        displayThread.join();
    }
    if (collisionListenerThread.joinable()) {
        collisionListenerThread.join();
    }
}

void Display::listenForCollisions() {
    std::cout << "Display: Collision listener started\n";
    
    while (running) {
        Message_inter_process msg;
        memset(&msg, 0, sizeof(msg));
        
        // Set up timeout for non-blocking receive
        struct sigevent event;
        SIGEV_UNBLOCK_INIT(&event);
        uint64_t timeout = 500000000ULL; // 500ms in nanoseconds
        TimerTimeout(CLOCK_MONOTONIC, _NTO_TIMEOUT_RECEIVE, &event, &timeout, NULL);
        
        int rcvid = MsgReceive(display_channel->chid, &msg, sizeof(msg), NULL);
        
        if (rcvid == -1) {
            // Timeout or error - continue loop
            continue;
        }
        
        if (rcvid == 0) {
            // This is a pulse, not a message
            continue;
        }
        
        // Reply immediately to unblock sender
        int reply = 0;
        MsgReply(rcvid, 0, &reply, sizeof(reply));
        
        // Process collision message
        if (msg.type == MessageType::COLLISION_DETECTED) {
            size_t numPairs = msg.dataSize / sizeof(std::pair<int, int>);
            std::pair<int, int>* pairs = reinterpret_cast<std::pair<int, int>*>(msg.data.data());
            
            // Lock and update collision set
            std::lock_guard<std::mutex> lock(collisionMutex);
            planesInCollision.clear();
            
            for (size_t i = 0; i < numPairs; i++) {
                printCollisionWarning(pairs[i].first, pairs[i].second);
                planesInCollision.insert(pairs[i].first);
                planesInCollision.insert(pairs[i].second);
            }
        }
    }
    
    std::cout << "Display: Collision listener stopped\n";
}

void Display::displayAircraft() {
    ATCTimer timer(1, 0); // 1 second interval
    std::cout << "Display: Aircraft display thread started\n";
    
    // Wait for planes to enter airspace
    while (running && shared_mem->is_empty.load()) {
        std::cout << "Display: Waiting for aircraft to enter airspace...\n";
        timer.waitTimer();
    }
    
    while (running) {
        // Check if airspace is now empty
        if (shared_mem->is_empty.load()) {
            std::cout << "\n";
            std::cout << "╔════════════════════════════════════════════════════════════╗\n";
            std::cout << "║     AIRSPACE EMPTY - ALL AIRCRAFT HAVE DEPARTED            ║\n";
            std::cout << "╚════════════════════════════════════════════════════════════╝\n";
            running = false;
            break;
        }
        
        // Read plane data from shared memory
        std::vector<msg_plane_info> planes;
        int count = shared_mem->count;
        
        for (int i = 0; i < count && i < 100; i++) {
            planes.push_back(shared_mem->plane_data[i]);
        }
        
        // Display the airspace grid
        printAirspaceGrid(planes);
        
        // Wait for next update
        timer.waitTimer();
    }
    
    std::cout << "Display: Aircraft display thread stopped\n";
}

void Display::clearScreen() {
    // ANSI escape sequence to clear screen and move cursor to top-left
    std::cout << "\033[2J\033[H";
}

void Display::printCollisionWarning(int planeA, int planeB) {
    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║                                                                  ║\n";
    std::cout << "║   *** COLLISION WARNING: Aircraft " << std::setw(2) << planeA 
              << " and " << std::setw(2) << planeB << " ***              ║\n";
    std::cout << "║                                                                  ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";
}

void Display::printAirspaceGrid(const std::vector<msg_plane_info>& planes) {
    std::lock_guard<std::mutex> lock(collisionMutex);
    
    std::cout << "\n";
    std::cout << "╔═══════════════════════════════════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║                           AIRSPACE DISPLAY - Timestamp: " 
              << std::setw(10) << shared_mem->timestamp << "                          ║\n";
    std::cout << "╠═══════════════════════════════════════════════════════════════════════════════════════════════╣\n";
    std::cout << "║  ID  │   Position X   │   Position Y   │   Position Z   │  Vel X  │  Vel Y  │  Vel Z  │ STAT ║\n";
    std::cout << "╠══════╪════════════════╪════════════════╪════════════════╪═════════╪═════════╪═════════╪══════╣\n";
    
    for (const auto& plane : planes) {
        bool inCollision = planesInCollision.find(plane.id) != planesInCollision.end();
        std::string status = inCollision ? " !!" : " OK";
        std::string prefix = inCollision ? "║ *" : "║  ";
        
        std::cout << prefix << std::setw(3) << plane.id << " │"
                  << std::setw(15) << std::fixed << std::setprecision(1) << plane.PositionX << " │"
                  << std::setw(15) << plane.PositionY << " │"
                  << std::setw(15) << plane.PositionZ << " │"
                  << std::setw(8) << std::setprecision(0) << plane.VelocityX << " │"
                  << std::setw(8) << plane.VelocityY << " │"
                  << std::setw(8) << plane.VelocityZ << " │"
                  << std::setw(5) << status << "║\n";
    }
    
    std::cout << "╠═══════════════════════════════════════════════════════════════════════════════════════════════╣\n";
    std::cout << "║  Total Aircraft: " << std::setw(3) << planes.size() 
              << "                                                                              ║\n";
    std::cout << "║  Airspace Boundaries: X[0-100000] Y[0-100000] Z[15000-40000] feet                             ║\n";
    std::cout << "║  Legend: * = Aircraft in collision warning | !! = Collision status                           ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════════════════════════════════════════════╝\n";
}
