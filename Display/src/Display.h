#ifndef DISPLAY_H_
#define DISPLAY_H_

#include <iostream>
#include <thread>
#include <atomic>
#include <vector>
#include <set>
#include <mutex>
#include <utility>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/dispatch.h>
#include <sys/neutrino.h>
#include <unistd.h>
#include <errno.h>
#include "Msg_structs.h"

// Display channel name
#define DISPLAY_CHANNEL_NAME "40247851_40228573_Display"

// Shared memory name
#define SHARED_MEMORY_NAME "/tmp/AH_40247851_40228573_Radar_shm"

class Display {
public:
    Display();
    ~Display();

    bool initialize();
    void run();
    void shutdown();

private:
    // Shared memory
    int shm_fd;
    SharedMemory* shared_mem;

    // IPC channel
    name_attach_t* display_channel;

    // Threads
    std::thread displayThread;
    std::thread collisionListenerThread;

    std::atomic<bool> running;

    // Collision tracking - stores ALL active collisions
    std::set<int> planesInCollision;                    // Set of plane IDs involved in any collision
    std::vector<std::pair<int, int>> collisionPairs;   // All active collision pairs
    std::mutex collisionMutex;                         // Protects collision data structures
    uint64_t lastCollisionTime;                        // Timestamp of most recent collision update

    // Initialization methods
    bool initializeSharedMemory();
    bool initializeIPCChannel();

    // Cleanup methods
    void cleanupSharedMemory();
    void cleanupIPCChannel();

    // Worker threads
    void displayAircraft();
    void listenForCollisions();

    // Display helper
    void printAirspaceGrid(const std::vector<msg_plane_info>& planes);
};

#endif /* DISPLAY_H_ */
