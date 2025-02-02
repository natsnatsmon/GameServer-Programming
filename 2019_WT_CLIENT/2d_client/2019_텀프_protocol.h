#pragma once

constexpr int MAX_USER = 10000;
constexpr int MAX_STR_LEN = 50;
constexpr int MAX_ID_LEN = 10;
//constexpr int NPC_ID_START = 10000;
constexpr int NUM_NPC = 7654;
constexpr int NUM_ITEM = 6;

#define WORLD_WIDTH		300
#define WORLD_HEIGHT	300

#define VIEW_RADIUS		10
#define NPC_VIEW_RADIUS 13

#define SERVER_PORT		3500

#define CS_LOGIN			1
#define CS_MOVE				2
#define CS_ATTACK			3
#define CS_CHAT				4
#define CS_LOGOUT			5
#define CS_CCU_TEST			6
#define CS_HOTSPOT_TEST		7

#define SC_LOGIN_OK			1
#define SC_LOGIN_FAIL		2
#define SC_POSITION			3
#define SC_CHAT				4
#define SC_STAT_CHANGE		5
#define SC_REMOVE_OBJECT	6
#define SC_ADD_OBJECT		7


#define PLAYER 0
#define NPC    1

// KIND
#define FAIRY  0
#define DEVIL  1
#define ANGEL  2
#define DRAGON 3

// TYPE
#define PEACE 1 
#define WAR   2

// MOVE TYPE
#define NO_MOVE  1
#define YES_MOVE 2

// ITEM
#define SUN_SET 0
#define CLOUD	1
#define MOON	2

#define CROWN	3
#define BREATH	4
#define SAINT_SWORD	5

#pragma pack(push ,1)

struct sc_packet_login_ok {
	char	size;
	char	type;
	int		id;
	char	kind;
	unsigned short	x, y;
	unsigned short	HP, LEVEL, ATTACK;
	int EXP, GOLD;
};

struct sc_packet_login_fail {
	char size;
	char type;
};

struct sc_packet_add_object {
	char size;
	char type;
	char obj_class;		// 1: PLAYER,    2:ORC,  3:Dragon, ��..
	int id;
	char kind;
	unsigned short x, y;
	unsigned short	HP, LEVEL;
	int EXP;

};

struct sc_packet_remove_object {
	char size;
	char type;
	char obj_class;
	int id;
};

struct sc_packet_position {
	char size;
	char type;
	char obj_class;
	int id;
	unsigned short x, y;
};

struct sc_packet_stat_change {
	char size;
	char type;
	char obj_class;
	int	id;
	unsigned short	HP, LEVEL;
	int EXP, GOLD;
};

struct sc_packet_chat {
	char size;
	char type;
	char obj_class;
	int	id;
	wchar_t	message[MAX_STR_LEN];
};




struct cs_packet_login {
	char	size;
	char	type;
	char player_kind;
	char player_id[10];
};

struct cs_packet_move {
	char	size;
	char	type;
	char	direction;		// 0:Up, 1:Down, 2:Left, 3:Right
};

struct cs_packet_attack {
	char	size;
	char	type;
};

struct cs_packet_chat {
	char	size;
	char	type;
	wchar_t	message[MAX_STR_LEN];
};

struct cs_packet_logout {
	char	size;
	char	type;
};

struct cs_packet_test {
	char	size;
	char	type;
};

#pragma pack (pop)