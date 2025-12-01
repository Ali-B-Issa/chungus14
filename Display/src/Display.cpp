#include "Display.h"
#include "ATCTimer.h"
#include <iomanip>
#include <sstream>
#include <cstring>
#include <cmath>



const double AIRSPACE_MIN_X = 0;
const double AIRSPACE_MAX_X = 100000;
const double AIRSPACE_MIN_Y = 0;
const double AIRSPACE_MAX_Y = 100000;

Display::Display() : shm_fd(-1), shared_mem(nullptr), display_channel(nullptr), running(false), lastCollisionTime(0) {}

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
    while (true) {
        shm_fd = shm_open(SHARED_MEMORY_NAME, O_RDONLY, 0666);
        if (shm_fd == -1) {
            std::cout << "Display: Waiting for shared memory...\n";
            sleep(1);
            continue;
        }

        shared_mem = (SharedMemory*)mmap(NULL, sizeof(SharedMemory), PROT_READ, MAP_SHARED, shm_fd, 0);

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
    displayThread = std::thread(&Display::displayAircraft, this);
    collisionListenerThread = std::thread(&Display::listenForCollisions, this);

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

        struct sigevent event;
        SIGEV_UNBLOCK_INIT(&event);
        uint64_t timeout = 500000000ULL;
        TimerTimeout(CLOCK_MONOTONIC, _NTO_TIMEOUT_RECEIVE, &event, &timeout, NULL);

        int rcvid = MsgReceive(display_channel->chid, &msg, sizeof(msg), NULL);

        if (rcvid == -1) continue;
        if (rcvid == 0) continue;

        int reply = 0;
        MsgReply(rcvid, 0, &reply, sizeof(reply));

        if (msg.type == MessageType::COLLISION_DETECTED) {
            size_t numPairs = msg.dataSize / sizeof(std::pair<int, int>);
            std::pair<int, int>* pairs = reinterpret_cast<std::pair<int, int>*>(msg.data.data());

            std::lock_guard<std::mutex> lock(collisionMutex);

            // **FIX: REPLACE collision data, don't accumulate**
            // Each message from ComputerSystem contains the COMPLETE current state
            planesInCollision.clear();
            collisionPairs.clear();

            // Update collision time
            lastCollisionTime = shared_mem->timestamp;

            // Add all collision pairs from the message
            for (size_t i = 0; i < numPairs; i++) {
                planesInCollision.insert(pairs[i].first);
                planesInCollision.insert(pairs[i].second);
                collisionPairs.push_back(pairs[i]);

                // Debug output
                //std::cout << "Display received collision: Plane " << pairs[i].first
                //          << " ⟷ Plane " << pairs[i].second << "\n";
            }

            //std::cout << "Display: Total collision pairs stored: " << collisionPairs.size() << "\n";
        }
    }

    std::cout << "Display: Collision listener stopped\n";
}

void Display::displayAircraft() {
    ATCTimer timer(1, 0);
    std::cout << "Display: Aircraft display thread started\n";

    while (running && shared_mem->is_empty.load()) {
        std::cout << "Display: Waiting for aircraft to enter airspace...\n";
        timer.waitTimer();
    }

    while (running) {
        if (shared_mem->is_empty.load()) {

            std::cout << "\n=== AIRSPACE EMPTY - ALL AIRCRAFT HAVE DEPARTED ===\n";
            running = false;
            break;
        }

        std::vector<msg_plane_info> planes;
        int count = shared_mem->count;

        for (int i = 0; i < count && i < 100; i++) {
            planes.push_back(shared_mem->plane_data[i]);
        }

        printAirspaceGrid(planes);
        timer.waitTimer();
    }

    std::cout << "Display: Aircraft display thread stopped\n";
}

void Display::printAirspaceGrid(const std::vector<msg_plane_info>& planes) {
    std::lock_guard<std::mutex> lock(collisionMutex);

    std::set<int> activePlaneIDs;
    for (const auto& plane : planes) {
        activePlaneIDs.insert(plane.id);
    }

    // Remove collision pairs involving planes that have left
    std::vector<std::pair<int, int>> validCollisionPairs;
    std::set<int> validPlanesInCollision;

    for (const auto& pair : collisionPairs) {
        // Check if both planes are still in airspace
        bool plane1Active = activePlaneIDs.find(pair.first) != activePlaneIDs.end();
        bool plane2Active = activePlaneIDs.find(pair.second) != activePlaneIDs.end();

        if (plane1Active && plane2Active) {
            // Both planes still in airspace - keep this collision pair
            validCollisionPairs.push_back(pair);
            validPlanesInCollision.insert(pair.first);
            validPlanesInCollision.insert(pair.second);
        }
    }
    
    collisionPairs = validCollisionPairs;
    planesInCollision = validPlanesInCollision;

    if (!collisionPairs.empty()) {

        std::cout << "ACTIVE COLLISION WARNINGS:\n";

        for (const auto& pair : collisionPairs) {
            std::cout << " Aircraft " << std::setw(2) << pair.first
                      << " Aircraft " << std::setw(2) << pair.second
                      << "\n";
        }

    }

    std::cout << "\n Aircraft Details:\n";
    std::cout << "-------------------------------------------------------------------------\n";

    for (const auto& plane : planes) {
        bool inCollision = planesInCollision.find(plane.id) != planesInCollision.end();

        std::vector<int> collisionPartners;
        if (inCollision) {
            for (const auto& pair : collisionPairs) {
                if (pair.first == plane.id) {
                    collisionPartners.push_back(pair.second);
                } else if (pair.second == plane.id) {
                    collisionPartners.push_back(pair.first);
                }
            }
        }

        std::cout << "  ID:" << std::setw(2) << plane.id
                  << " Pos(" << std::setw(6) << (int)plane.PositionX << ","
                  << std::setw(6) << (int)plane.PositionY << ","
                  << std::setw(6) << (int)plane.PositionZ << ")"
                  << " Vel(" << std::setw(4) << (int)plane.VelocityX << ","
                  << std::setw(4) << (int)plane.VelocityY << ","
                  << std::setw(4) << (int)plane.VelocityZ << ")";

        if (inCollision && !collisionPartners.empty()) {
            std::cout << " COLLISION WITH: Plane";
            for (size_t i = 0; i < collisionPartners.size(); i++) {
                if (i > 0) std::cout << ",";
                std::cout << " " << collisionPartners[i];
            }
        }
        std::cout << "\n";
    }

    std::cout << "\n";
}

