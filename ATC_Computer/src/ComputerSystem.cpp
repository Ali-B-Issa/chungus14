#include "ComputerSystem.h"
#include "ATCTimer.h"
#include <ctime>        // For std::time_t, std::localtime
#include <iomanip>      // For std::put_time
#include <cmath>
#include <sys/dispatch.h>
#include "Msg_structs.h"
#include <cstring> // For memcpy

// COEN320 Task 3.1, set the display channel name
#define display_channel_name "40247851_40228573_Display"


ComputerSystem::ComputerSystem() : shm_fd(-1), shared_mem(nullptr), running(false) {}

ComputerSystem::~ComputerSystem() {
    joinThread();
    cleanupSharedMemory();
}

bool ComputerSystem::initializeSharedMemory() {
	// Open the shared memory object
	while (true) {
		 // COEN320 Task 3.2
		// Attempt to open the shared memory object (You need to use the same name as Task 2 in Radar)
		shm_fd = shm_open("/tmp/AH_40247851_40228573_Radar_shm", O_RDONLY, 0666);

		if (shm_fd == -1) {
			std::cerr << "Failed to open shared memory, retrying..." << std::endl;
			sleep(1);  // Wait before retrying to give radar time to create if needed
			continue;
		}

		// COEN320 Task 3.3
		// Map the shared memory object into the process's address space
		shared_mem = (SharedMemory*)mmap(NULL, sizeof(SharedMemory), PROT_READ, MAP_SHARED, shm_fd, 0);

		if (shared_mem == MAP_FAILED) {
			std::cerr << "Failed to map shared memory, retrying..." << std::endl;
			close(shm_fd);
			shm_fd = -1;
			sleep(1);
			continue;
		}

		//std::cout << "Shared memory initialized successfully" << std::endl;
		return true;

	}
}

void ComputerSystem::cleanupSharedMemory() {
    if (shared_mem && shared_mem != MAP_FAILED) {
        munmap(shared_mem, sizeof(SharedMemory));
    }
    if (shm_fd != -1) {
        close(shm_fd);
    }
}

bool ComputerSystem::startMonitoring() {
    if (initializeSharedMemory()) {
        running = true;
        //std::cout << "Starting monitoring thread." << std::endl;
        monitorThread = std::thread(&ComputerSystem::monitorAirspace, this);
        return true;
    } else {
        std::cerr << "Failed to initialize shared memory. Monitoring not started.\n";
        return false;
    }
}

void ComputerSystem::joinThread() {
    if (monitorThread.joinable()) {
        monitorThread.join();
    }
}

void ComputerSystem::monitorAirspace() {
	//std::cout << "Initial is_empty value: " << shared_mem->is_empty.load() << std::endl;
	ATCTimer timer(1,0);
	// Vector to store plane data
	std::vector<msg_plane_info> plane_data_vector;
	uint64_t timestamp;
    // Keep monitoring indefinitely until `stopMonitoring` is called
	while (shared_mem->is_empty.load()) {
		std::cout << "Waiting for planes in airspace...\n";
		timer.waitTimer();
	}

	while (running) {
		if (shared_mem->is_empty.load()) {
			std::cout << "No planes in airspace. Stopping monitoring.\n";
			running = false;
	        break;
        } else {
        	plane_data_vector.clear();

            timestamp = shared_mem->timestamp;
            //std::cout << "Last Update Timestamp: " << timestamp << "\n";
            //std::cout << "Number of planes in shared memory: " << shared_mem->count << "\n";

            for (int i = 0; i < shared_mem->count; ++i) {
            	const msg_plane_info& plane = shared_mem->plane_data[i];
            	// Store the plane info in the vector
            	plane_data_vector.push_back(plane);
            }
        }

		if (plane_data_vector.size()>1)
            checkCollision(timestamp, plane_data_vector);
		else
           // std::cout << "No collision possible with single plane\n";
        // Sleep for a short interval before the next poll
       timer.waitTimer();
    }
	std::cout << "Exiting monitoring loop." << std::endl;
}

void ComputerSystem::checkCollision(uint64_t currentTime, std::vector<msg_plane_info> planes) {
    // COEN320 Task 3.4
    // detect collisions between planes in the airspace within the time constraint

    std::vector<std::pair<int, int>> collisionPairs;

    // Check ALL pairs of planes
    for (size_t i = 0; i < planes.size(); i++) {
    	for (size_t j = i + 1; j < planes.size(); j++) {
    		// Check if planes will collide
    		if (checkAxes(planes[i], planes[j])) {
    			collisionPairs.emplace_back(planes[i].id, planes[j].id);
    			
    			// Debug output - ENABLE THIS to see what ComputerSystem detects
    			std::cout << "*** ComputerSystem detected collision: Plane " 
    			          << planes[i].id << " ⟷ Plane " << planes[j].id << " ***\n";
    		}
    	}
    }

    // Debug output - show total pairs detected
    if (!collisionPairs.empty()) {
        std::cout << "ComputerSystem: Total collision pairs detected: " 
                  << collisionPairs.size() << "\n";
    }

    // COEN320 Task 3.5
    // In the case of collision send message to Display system
    if (!collisionPairs.empty()) {

    	Message_inter_process msg_to_send;

    	size_t numPairs = collisionPairs.size();
    	size_t dataSize = numPairs * sizeof(std::pair<int, int>);

    	msg_to_send.header = true;  // Inter-process message
    	msg_to_send.planeID = -1;
    	msg_to_send.type = MessageType::COLLISION_DETECTED;
    	msg_to_send.dataSize = dataSize;
    	
    	// Verify we're copying all pairs
    	std::cout << "ComputerSystem: Sending " << numPairs << " collision pairs to Display\n";
    	std::memcpy(msg_to_send.data.data(), collisionPairs.data(), dataSize);

    	sendCollisionToDisplay(msg_to_send);
    }
    
}

bool ComputerSystem::checkAxes(msg_plane_info plane1, msg_plane_info plane2) {
    // COEN320 Task 3.4
    // A collision is defined as two planes entering the defined airspace constraints within the time constraint
    
    // Calculate the distance between the two planes in each axis (current position)
    double deltaX = std::abs(plane1.PositionX - plane2.PositionX);
    double deltaY = std::abs(plane1.PositionY - plane2.PositionY);
    double deltaZ = std::abs(plane1.PositionZ - plane2.PositionZ);

    // Check if the planes are currently within the constraint distances
    if (deltaX < CONSTRAINT_X && deltaY < CONSTRAINT_Y && deltaZ < CONSTRAINT_Z) {
        // Planes are too close right now - collision detected
        return true;
    }

    // Predict positions after time constraint
    double futureX1 = plane1.PositionX + plane1.VelocityX * timeConstraintCollisionFreq;
    double futureY1 = plane1.PositionY + plane1.VelocityY * timeConstraintCollisionFreq;
    double futureZ1 = plane1.PositionZ + plane1.VelocityZ * timeConstraintCollisionFreq;

    double futureX2 = plane2.PositionX + plane2.VelocityX * timeConstraintCollisionFreq;
    double futureY2 = plane2.PositionY + plane2.VelocityY * timeConstraintCollisionFreq;
    double futureZ2 = plane2.PositionZ + plane2.VelocityZ * timeConstraintCollisionFreq;

    // Check if future positions will be within constraints
    double futureDeltaX = std::abs(futureX1 - futureX2);
    double futureDeltaY = std::abs(futureY1 - futureY2);
    double futureDeltaZ = std::abs(futureZ1 - futureZ2);

    if (futureDeltaX < CONSTRAINT_X && futureDeltaY < CONSTRAINT_Y && futureDeltaZ < CONSTRAINT_Z) {
        // Planes will be too close in the future - collision predicted
        return true;
    }

    // No collision detected
    return false;
}


void ComputerSystem::sendCollisionToDisplay(const Message_inter_process& msg){
	int display_channel = name_open(display_channel_name, 0);
	if (display_channel == -1) {
		std::cerr << "Computer system: Error opening display channel: " << strerror(errno) << "\n";
		return;
	}
	int reply;

	int status = MsgSend(display_channel, &msg, sizeof(msg), &reply, sizeof(reply));
	if (status == -1) {
		std::cerr << "Computer system: Error sending to display: " << strerror(errno) << "\n";
	} else {
		std::cout << "ComputerSystem: Successfully sent collision message to Display\n";
	}
	name_close(display_channel);
}
