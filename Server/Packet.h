#pragma once
#include <memory>
#include <iostream>
#include <fstream>

const int EmptyPktSize = 4;					//Number of data bytes in a packet with no data field

class Packet
{
	struct Header
	{
		unsigned char SourceID;					// Source ID
		unsigned char Offset;					// The offset to finding the edge of the head
		unsigned char Instruction;				// The instruction byte
		unsigned char BodyLength;				// Size of the message
	} Head;
	char* Data;									//The data bytes

	char* TxBuffer;

public:
	unsigned char getInstruction() const { return Head.Instruction; } // We do this because "Header" is a private struct in this case

	Packet() : Data(nullptr), TxBuffer(nullptr) { memset(&Head, 0, sizeof(Head));  Head.SourceID = 2; };		//Default Constructor - Safe State


    // ************* TEMPORARY FUNCTIONS *************
    // Replace these functions with the proper versions, this was just used to do multithreading testing
    Packet(char* src) {
        // copy first 4 bytes into Head
        memcpy(&Head, src, sizeof(Head));
        // if BodyLength > 0, allocate and copy Data
        if (Head.BodyLength > 0) {
            Data = new char[Head.BodyLength];
            memcpy(Data, src + sizeof(Head), Head.BodyLength);
        }
    }

    void SetData(char* srcData, int Size) {
        Head.Instruction = srcData[0]; // first byte is the instruction flag
        Head.BodyLength = Size;
        Data = new char[Size];
        memcpy(Data, srcData, Size);
    }

    char* SerializeData(int& TotalSize) {
        TotalSize = EmptyPktSize + Head.BodyLength;
        TxBuffer = new char[TotalSize];
        memcpy(TxBuffer, &Head, EmptyPktSize);
        if (Head.BodyLength > 0)
            memcpy(TxBuffer + EmptyPktSize, Data, Head.BodyLength);
        return TxBuffer;
    }

};
