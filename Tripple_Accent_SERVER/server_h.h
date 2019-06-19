#pragma once
#define UNICODE  
#include <sqlext.h>  
#include <chrono>

extern "C" {
#include "include\lua.h"
#include "include\lauxlib.h"
#include "include\lualib.h"
}


enum EVENT_TYPE { EV_PLAYER_MOVE_DETECT, EV_MOVE, EV_NPC_ATTACK, EV_NPC_RESURRECTION,
				  EV_HEAL, EV_ATTACK, EV_RESURRECTION,
				  EV_RECV, EV_SEND,
			  	  DB_EVT_SEARCH, DB_EVT_SAVE, DB_EVT_UPDATE };

struct EVENT_ST {
	int obj_id;
	EVENT_TYPE type;
	high_resolution_clock::time_point  start_time;

	constexpr bool operator < (const EVENT_ST& _Left) const
	{	// apply operator< to operands
		return (start_time > _Left.start_time);
	}
};

struct DB_EVENT_ST {
	int client_id;
	EVENT_TYPE type;
};

// function list
void error_display(const char *mess, int err_no);
void db_err_display(SQLHANDLE hHandle, SQLSMALLINT hType, RETCODE RetCode);

void Initialize_PC();
void Initialize_NPC();

void worker_thread();
void do_recv(int id);
int do_accept();
void do_timer();
void do_transaction();
void disconnect_client(int id);

void add_timer(int obj_id, EVENT_TYPE et, high_resolution_clock::time_point start_time);
void add_db_evt(int client_id, EVENT_TYPE et);

bool search_user_id(int client);
bool save_user_data(int client);

bool is_sleeping(int id);
bool is_player_player_eyesight(int client, int other_client);
bool is_player_npc_eyesight(int client, int npc_id);
bool is_npc_eyesight(int client_id, int npc_id);
bool is_near_npc(int client, int npc);
bool is_level_up(int client);

double kind_effect(int client, int npc);
double npc_kind_effect(int client, int npc);
double equip_effect(int client, int npc);
double item_effect(int client);

int cal_hp(int client);

wchar_t* get_NPC_name(int npc_id);
void wakeup_NPC(int id);
void random_move_NPC(int id);

int API_load_NPC_info(lua_State *L);
int API_get_player_x(lua_State *L);
int API_get_player_y(lua_State *L);
int API_get_npc_x(lua_State *L);
int API_get_npc_y(lua_State *L);
int API_send_message(lua_State *L);

void process_packet(int client, char *packet);
void process_event(EVENT_ST &ev);
void process_db_event(DB_EVENT_ST &ev);

void send_packet(int client, void *packet);
void send_login_ok_packet(int new_id);
void send_login_fail_packet(int new_id);
void send_put_player_packet(int client, int new_id);
void send_put_npc_packet(int client, int new_npc_id);
void send_remove_player_packet(int client, int remove_id);
void send_remove_npc_packet(int client, int remove_npc_id);
void send_player_pos_packet(int client, int pl);
void send_npc_pos_packet(int client, int npc);
void send_player_stat_change_packet(int client, int player);
void send_npc_stat_change_packet(int client, int npc);
void send_system_chat_packet(int client, int from_id, wchar_t *mess);
void send_player_chat_packet(int client, int from_id, wchar_t *mess);
void send_npc_chat_packet(int client, int from_id, wchar_t *mess);
