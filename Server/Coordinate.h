#pragma once
#include <math.h>
#include <cstring>

class Coordinate
{
private:
	struct point
	{
		double X;
		double Y;
		double Z;
	}P;
	
public:
	Coordinate();
	Coordinate(double X_in, double Y_in, double Z_in);
	double get_X();
	void set_X(double X_in);
	double get_Y();
	void set_Y(double Y_in);
	double get_Z();
	void set_Z(double Z_in);
	double get_distance(Coordinate Target);
	void copy_to_Buffer(char* buffer);
};

	

