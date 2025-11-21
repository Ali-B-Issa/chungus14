/*
 * CommunicationsSystem.cpp
 *
 *  Created on: Nov 18, 2025
 *      Author: a_i49449
 */
#include "CommunicationsSystem.h"
#include "ATCTimer.h"
#include <ctime>
#include <iomanip>
#include <cmath>
#include <sys/dispatch.h>
#include <cstring>

#define COMMS_CHANNEL_NAME "AH_40247851_40228573_Comms"

CommunicationsSystem::CommunicationsSystem() : comms_channel(nullptr), stopThread(false) {
    Communications_System = std::thread(&CommunicationsSystem::HandleCommunications, this);
}

CommunicationsSystem::~CommunicationsSystem() {
    stopThread = true;
    
    // Detach channel to unblock MsgReceive
    if (comms_channel) {
        name_detach(comms_channel, 0);
        comms_channel = nullptr;
    }
    
    if (Communications_System.joinable()) {
        Communications_System.join();
    }
}

void CommunicationsSystem::HandleCommunications() {
    std::cout << "Communications System started\n";

    // Try to create channel with retries
    int retries = 5;
    while (retries > 0 && comms_channel == NULL) {
        comms_channel = name_attach(NULL, COMMS_CHANNEL_NAME, 0);
        
        if (comms_channel == NULL) {
            std::cerr << "Failed to create Communications System channel, retrying... (" 
                      << strerror(errno) << ")\n";
            sleep(1);
            retries--;
        }
    }
    
    if (comms_channel == NULL) {
        std::cerr << "Failed to create Communications System channel after retries\n";
        std::cerr << "Error: " << strerror(errno) << "\n";
        return;
    }

    std::cout << "Communications System listening on channel: " << COMMS_CHANNEL_NAME << "\n";

    while (!stopThread) {
        Message_inter_process msg;
        memset(&msg, 0, sizeof(msg));
        
        // Set timeout so we can check stopThread flag
        struct sigevent event;
        SIGEV_UNBLOCK_INIT(&event);
        uint64_t timeout = 500000000ULL; // 500ms
        TimerTimeout(CLOCK_MONOTONIC, _NTO_TIMEOUT_RECEIVE, &event, &timeout, NULL);
        
        int rcvid = MsgReceive(comms_channel->chid, &msg, sizeof(msg), NULL);

        if (rcvid == -1) {
            continue;
        }

        if (rcvid == 0) {
            continue;
        }

        if (!msg.header) {
            MsgReply(rcvid, 0, NULL, 0);
            continue;
        }

        std::cout << "Communications System received message:\n";
        std::cout << "  Plane ID: " << msg.planeID << "\n";
        std::cout << "  Type: " << static_cast<int>(msg.type) << "\n";
        std::cout << "  Data Size: " << msg.dataSize << "\n";

        int reply = 0;
        MsgReply(rcvid, 0, &reply, sizeof(reply));

        switch (msg.type) {
            case MessageType::REQUEST_CHANGE_OF_HEADING:
                std::cout << "Forwarding heading change request to Plane " << msg.planeID << "\n";
                messageAircraft(msg);
                break;

            case MessageType::REQUEST_CHANGE_POSITION:
                std::cout << "Forwarding position change request to Plane " << msg.planeID << "\n";
                messageAircraft(msg);
                break;

            case MessageType::REQUEST_CHANGE_ALTITUDE:
                std::cout << "Forwarding altitude change request to Plane " << msg.planeID << "\n";
                messageAircraft(msg);
                break;

            case MessageType::EXIT:
                std::cout << "Exit command received\n";
                stopThread = true;
                break;

            default:
                std::cerr << "Unknown message type received: " << static_cast<int>(msg.type) << "\n";
                break;
        }
    }

    if (comms_channel) {
        name_detach(comms_channel, 0);
        comms_channel = nullptr;
    }
    
    std::cout << "Communications System stopped\n";
}

void CommunicationsSystem::messageAircraft(const Message_inter_process& msg) {
    std::string plane_channel_name = "AH_40247851_40228573_" + std::to_string(msg.planeID);
    int plane_channel = name_open(plane_channel_name.c_str(), 0);

    if (plane_channel == -1) {
        std::cerr << "Failed to open channel to Plane " << msg.planeID << " (" << plane_channel_name << ")\n";
        std::cerr << "  Error: " << strerror(errno) << "\n";
        std::cerr << "  Plane may have left airspace or channel doesn't exist yet\n";
        return;
    }

    std::cout << "Successfully opened channel to Plane " << msg.planeID << "\n";

    int reply;
    if (MsgSend(plane_channel, &msg, sizeof(msg), &reply, sizeof(reply)) == -1) {
        std::cerr << "Failed to send message to Plane " << msg.planeID << "\n";
        std::cerr << "  Error: " << strerror(errno) << "\n";
    } else {
        std::cout << "Successfully sent command to Plane " << msg.planeID << "\n";
    }

    name_close(plane_channel);
}
