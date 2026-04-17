#pragma once
#include <iostream>
#include <fstream>
#include <string>
#include <ctime>
#include <mutex>

using namespace std;

extern mutex fileMutex;
extern mutex consoleMutex;

class Logger {
public:
    void Log(const string& message);

    // Data is logged upon both receiving and sending data
    void LogReceive(const string& data);
    void LogSend(const string& data);

private:
    // Retreives and formats a timestamp for each new log entry
    string CurrentTime();

    // Shortens the output if it's too long
    string Truncate(const string& data);

    // Formats packet logging into a stable, machine-checkable text shape
    string FormatPacketLog(const string& direction, const string& data);
};
