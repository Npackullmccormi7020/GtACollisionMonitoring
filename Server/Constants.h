#pragma once
#include <atomic>
#include <iostream>
#include <string>

// Packet Flag Definitions
constexpr unsigned char FLIGHT_DONE = 0x00;
constexpr unsigned char FLIGHT_ACTIVE = 0x77; // switched 0x11 to 0x77 because 0x11 is an unprintable character
constexpr unsigned char COLLISION_ALERT = 0x22;
constexpr unsigned char FLIGHT_ALERT_RESPONSE = 0x33;
constexpr unsigned char ACK = 0x44;
constexpr unsigned char DATA_START = 0x55;
constexpr unsigned char DATA_CHUNK = 0x66;