#ifndef DISPLAY_H_
#define DISPLAY_H_

#include <iostream>
#include <thread>
#include <atomic>
#include <vector>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/dispatch.h>
#include <unistd.h>
#include <set>
#include "Msg_structs.h"

// Display channel name - must match ComputerSystem.cpp
#define DISPLAY_CHANNEL_NAME "40247851_40228573_Display"

// Shared memory name - must match Radar.cpp
#define SHARED_MEMORY_NAME "/tmp/AH_40247851_40228573_Radar_shm"

class Display {
public:
    Display();
    ~Display();

    bool initialize();
    void run();
    void shutdown();

private:
    // Shared memory for reading radar data
    int shm_fd;
    SharedMemory* shared_mem;

    // IPC channel for collision notifications
    name_attach_t* display_channel;

    // Threads
    std::thread displayThread;
    std::thread collisionListenerThread;

    // Control flags
    std::atomic<bool> running;

    // Track planes involved in collisions for highlighting
    std::set<int> planesInCollision;
    std::mutex collisionMutex;

    // Methods
    bool initializeSharedMemory();
    bool initializeIPCChannel();
    void cleanupSharedMemory();
    void cleanupIPCChannel();

    void displayAircraft();          // Thread: periodically display aircraft
    void listenForCollisions();      // Thread: listen for collision messages

    void printAirspaceGrid(const std::vector<msg_plane_info>& planes);
    void printCollisionWarning(int planeA, int planeB);
    void clearScreen();
};

#endif /* DISPLAY_H_ */
