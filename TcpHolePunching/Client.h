#pragma once

#include "Address.h"

class Client
{
public:
	int CreateSocket();

private:
	void Connect(const Address& address);
	void Accept(USHORT port);
};

