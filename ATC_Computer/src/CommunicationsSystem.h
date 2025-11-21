/*
 * CommunicationsSystem.h
 *
 *  Created on: Nov 18, 2025
 *      Author: a_i49449
 */

#ifndef SRC_COMMUNICATIONSSYSTEM_H_
#define SRC_COMMUNICATIONSSYSTEM_H_

#include <iostream>
#include <thread>
#include <atomic>
#include <sys/dispatch.h>
#include "Msg_structs.h"

class CommunicationsSystem {
public:
    CommunicationsSystem();
    ~CommunicationsSystem();
    
private:
    void HandleCommunications();
    void messageAircraft(const Message_inter_process& msg);
    
    std::thread Communications_System;
    name_attach_t* comms_channel;
    std::atomic<bool> stopThread;
};

#endif /* SRC_COMMUNICATIONSSYSTEM_H_ */
