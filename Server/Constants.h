#pragma once
#include <atomic>
#include <iostream>
#include <string>

// Packet Flag Definitions
constexpr unsigned char FLIGHT_DONE = 0x00;    // TODO: replace 0x00 with your chosen value
constexpr unsigned char FLIGHT_ACTIVE = 0x11;    // TODO: replace 0xAA with your chosen value
constexpr unsigned char COLLISION_ALERT = 0x22;    // TODO: replace 0xAA with your chosen value
constexpr unsigned char FLIGHT_ALERT_RESPONSE = 0x33;    // TODO: replace 0xAA with your chosen value
constexpr unsigned char ACK = 0x44;    // TODO: replace 0xAA with your chosen value