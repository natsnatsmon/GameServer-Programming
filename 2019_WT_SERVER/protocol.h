#pragma once

constexpr int MAX_USER = 500;
constexpr int MAX_NPC = 20000;
constexpr int MAX_STR_LEN = 50; // 크게하면 패킷크기 넘어가니까 50 이하의 숫자를 넣도록! 유니코드니까 50하면 100바이트 쓰는거임

constexpr int WORLD_WIDTH = 800;
constexpr int WORLD_HEIGHT = 800;


constexpr int SERVER_PORT = 3500;

constexpr int CS_ID		= 0;
constexpr int CS_UP		= 1;
constexpr int CS_DOWN	= 2;
constexpr int CS_LEFT	= 3;
constexpr int CS_RIGHT	= 4;

#pragma pack(push, 1)

struct cs_packet_id {
	char size;
	char type;
	char id[20];
};

struct cs_packet_up {
	char size;
	char type;
};

struct cs_packet_down {
	char size;
	char type;
}; 

struct cs_packet_left {
	char size;
	char type;
}; 

struct cs_packet_right {
	char size;
	char type;
}; 

constexpr int SC_LOGIN_OK		= 1; // 서버에 접속한걸 관대하게 받아주겠다~
constexpr int SC_PUT_PLAYER		= 2; // 플레이어 집어 넣는다~
constexpr int SC_REMOVE_PLAYER	= 3; // 클라가 게임 접속 종료했다~
constexpr int SC_MOVE_PLAYER	= 4; // 플레이어가 새 좌표로 이동했으니 표시하는 좌표를 바꿔랑
constexpr int SC_PUT_NPC		= 5;
constexpr int SC_REMOVE_NPC		= 6;
constexpr int SC_MOVE_NPC		= 7;
constexpr int SC_CHAT			= 8;
constexpr int SC_NPC_CHAT		= 9;

struct sc_packet_login_ok {
	char size;
	char type;
	char id;
};

struct sc_packet_put_player {
	char size;
	char type;
	int id;
	int x, y;
};

struct sc_packet_remove_player {
	char size;
	char type;
	int id;
};

struct sc_packet_move_player {
	char size;
	char type;
	int id;
	int x, y;
};

struct sc_packet_chat {
	char size;
	char type;
	int id;
	wchar_t message[MAX_STR_LEN];
};

#pragma pack(pop)