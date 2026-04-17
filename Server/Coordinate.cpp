#include "Coordinate.h"

Coordinate::Coordinate() : P{ 0.0, 0.0, 0.0 } {}

Coordinate::Coordinate(double X_in, double Y_in, double Z_in)
{
	this->P.X = X_in;
	this->P.Y = Y_in;
	this->P.Z = Z_in;
}

double Coordinate::get_X() const
{
	return P.X;
}
void Coordinate::set_X(double X_in)
{
	this->P.X = X_in;
}
double Coordinate::get_Y() const
{
	return P.Y;
}
void Coordinate::set_Y(double Y_in)
{
	this->P.Y = Y_in;
}
double Coordinate::get_Z() const
{
	return P.Z;
}
void Coordinate::set_Z(double Z_in)
{
	this->P.Z = Z_in;
}
double Coordinate::get_distance(Coordinate Target) const
{
	double result = sqrt((Target.get_X() - P.X) * (Target.get_X() - P.X) + (Target.get_Y() - P.Y) * (Target.get_Y() - P.Y) + (Target.get_Z() - P.Z) * (Target.get_Z() - P.Z));
	return result;
}

void Coordinate::copy_to_Buffer(char* buffer) const
{
	memcpy(buffer, &P, sizeof(P));
}

void Coordinate::copy_from_Buffer(char* buffer)
{
	memcpy(&P, buffer, sizeof(P));
}
