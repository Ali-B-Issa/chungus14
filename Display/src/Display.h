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

    // Initialize shared memory and IPC channel
    bool initialize();

    // Run the display (blocks until shutdown)
    void run();

    // Shutdown the display system
    void shutdown();

private:
    // Shared memory for reading radar data
    int shm_fd;
    SharedMemory* shared_mem;

    // IPC channel for collision notifications from ComputerSystem
    name_attach_t* display_channel;

    // Threads
    std::thread displayThread;           // Thread for displaying aircraft
    std::thread collisionListenerThread; // Thread for listening to collisions

    // Control flags
    std::atomic<bool> running;

    // Track planes involved in collisions for highlighting
    std::set<int> planesInCollision;
    std::vector<std::pair<int, int>> collisionPairs;  // Store which planes collide with which
    std::mutex collisionMutex;
    uint64_t lastCollisionTime;  // Timestamp of last collision message


    bool initializeSharedMemory();
    bool initializeIPCChannel();


    void cleanupSharedMemory();
    void cleanupIPCChannel();


    void displayAircraft();          // Periodically display aircraft positions
    void listenForCollisions();      // Listen for collision messages from ComputerSystem


    void printAirspaceGrid(const std::vector<msg_plane_info>& planes);

};

#endif /* DISPLAY_H_ */
