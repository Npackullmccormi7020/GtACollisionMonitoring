#include "Logging.h"
#define maxLen 100

mutex fileMutex;
mutex consoleMutex;

void Logger::Log(const string& message) {
    string log = CurrentTime() + " - " + message;

    {
        lock_guard<mutex> lock(consoleMutex);
        cout << log << endl;
    }

    // Append to log file
    {
        lock_guard<mutex> lock(fileMutex);
        ofstream file("log.txt", ios::app);
        if (file.is_open()) {
            file << log << endl;
            file.close();
        }
    }
}

// Data is logged upon both receiving and sending data
void Logger::LogReceive(const string& data) { Log("RECEIVED: " + Truncate(data)); }
void Logger::LogSend(const string& data) { Log("SENT: " + Truncate(data)); }

// Retreives and formats a timestamp for each new log entry
string Logger::CurrentTime() {
    time_t now = time(0);
    char buffer[26];
    ctime_s(buffer, sizeof(buffer), &now);
    buffer[strcspn(buffer, "\n")] = '\0'; // Remove newline character
    return string(buffer);
}

// Shortens the output if it's too long
string Logger::Truncate(const string& data) {
    if (data.length() > maxLen) { return data.substr(0, maxLen) + "..."; }
    return data;
};
