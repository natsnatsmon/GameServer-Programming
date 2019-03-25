#pragma once


#define SERVERPORT 9000
#define MAX_BUFFER 1024
#define MAX_PLAYER 10

#define C_PACKET_SIZE 4
#define S_PACKET_SIZE 82

#define UP 0
#define DOWN 1
#define RIGHT 2
#define LEFT 3

struct Key {
	bool keyDown[4];
};

struct Pos {
	float x; float y;
};

#pragma pack(push, 1)
struct SOCKETINFO {
	WSAOVERLAPPED overlapped;
	WSABUF dataBuffer;
	SOCKET socket;
	char buf[MAX_BUFFER];

	int receiveBytes = C_PACKET_SIZE;
	int sendBytes = S_PACKET_SIZE;
	int playerId;
};
#pragma pack(pop, 1)

#pragma pack(push, 1)
// 80 + 4
struct Packet {
	Pos player_pos[MAX_PLAYER];
	short player_num;
};
#pragma pack(pop, 1)


// 플레이어 10명의 키입력, 10명의 포지션 필요
// 플레이어 ID 필요, 총 플레이어 수 필요