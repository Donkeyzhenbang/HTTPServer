#pragma once

#include "protocol.h"

// Sends a heartbeat request (Client -> Server, Type 0x09)
int SendHeartbeatRequest(int fd);

// Sends a heartbeat response (Server -> Client, Type 0x0A)
int SendHeartbeatResponse(int fd);

// Receives and parses a heartbeat frame
// Returns: 0 on success, <0 on failure (-1 read error, -2 parse error, -3 wrong type)
// outFrame: filled with parsed data if success
int ReceiveHeartbeatFrame(int fd, HeartbeatFrame* outFrame);
