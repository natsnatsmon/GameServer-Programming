#pragma once

constexpr int MAX_USER = 10;
constexpr int MAX_NPC = 20000;
constexpr int MAX_STR_LEN = 50; // ũ���ϸ� ��Ŷũ�� �Ѿ�ϱ� 50 ������ ���ڸ� �ֵ���! �����ڵ�ϱ� 50�ϸ� 100����Ʈ ���°���

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

constexpr int SC_LOGIN_OK		= 1; // ������ �����Ѱ� �����ϰ� �޾��ְڴ�~
constexpr int SC_PUT_PLAYER		= 2; // �÷��̾� ���� �ִ´�~
constexpr int SC_REMOVE_PLAYER	= 3; // Ŭ�� ���� ���� �����ߴ�~
constexpr int SC_MOVE_PLAYER	= 4; // �÷��̾ �� ��ǥ�� �̵������� ǥ���ϴ� ��ǥ�� �ٲ��
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