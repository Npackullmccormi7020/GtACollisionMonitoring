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
	Packet() : Data(nullptr), TxBuffer(nullptr) { memset(&Head, 0, sizeof(Head));  Head.SourceID = 2; };		//Default Constructor - Safe State

	Packet(char* src) : Data(nullptr), TxBuffer(nullptr)
	{

	}

	void SetData(char* srcData, int Size)
	{

	};

	char* SerializeData(int& TotalSize)
	{

	};

};
