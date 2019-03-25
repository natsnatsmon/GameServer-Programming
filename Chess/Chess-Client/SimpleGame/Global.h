#pragma once
#include <stdio.h>

#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <WinSock2.h>
#pragma comment(lib, "ws2_32")


#include <iostream>
#include <Windows.h>
#include <stdlib.h>



#define SERVERPORT 9000

#define MAX_PLAYER 10

#define C_PACKET_SIZE 4
#define S_PACKET_SIZE 82

#define UP 0
#define DOWN 1
#define RIGHT 2
#define LEFT 3


#define PLAYER_ID 0
#define MAX_OBJECTS 2

struct Key {
	bool keyDown[4];
};

struct Pos {
	float x; float y;
};

#pragma pack(push, 1)
struct Packet {
	Pos player_pos[MAX_PLAYER];
	short player_num;
};
#pragma pack(pop)
