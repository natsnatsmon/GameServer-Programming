#include <iostream>
#include <unordered_set>
#include <vector>
#include <thread>
#include <chrono>
#include <queue>
#include <mutex>
using namespace std;
using namespace chrono;
#include <locale.h>
#include <winsock2.h>

extern "C" {
#include "include\lua.h"
#include "include\lauxlib.h"
#include "include\lualib.h"
}

#include "protocol.h"

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "lua53.lib")

#define MAX_BUFFER        1024
#define VIEW_RADIUS       3   

enum EVENT_TYPE { EV_PLAYER_MOVE_DETECT, EV_MOVE, EV_HEAL, EV_RECV, EV_SEND };

struct OVER_EX {
	WSAOVERLAPPED	over;
	WSABUF			dataBuffer;
	char			messageBuffer[MAX_BUFFER];
	EVENT_TYPE		event_t;
	int				target_player;
};

class SOCKETINFO
{
public:
	bool in_use;
	OVER_EX over_ex;
	SOCKET socket;
	char packet_buffer[MAX_BUFFER];
	int	prev_size;
	short x, y;
	bool is_sleeping;
	unordered_set <int> view_list;
	lua_State *L;
	mutex l_lock;
	mutex v_lock;
	bool is_login;

	SOCKETINFO() {
		in_use = false;
		over_ex.dataBuffer.len = MAX_BUFFER;
		over_ex.dataBuffer.buf = over_ex.messageBuffer;
		over_ex.event_t = EV_RECV;

		L = luaL_newstate();
		luaL_openlibs(L);

		luaL_loadfile(L, "monster_ai.lua");
		lua_pcall(L, 0, 0, 0);
	}
};

struct EVENT_ST {
	int obj_id;
	EVENT_TYPE type;
	high_resolution_clock::time_point  start_time;

	constexpr bool operator < (const EVENT_ST& _Left) const
	{	// apply operator< to operands
		return (start_time > _Left.start_time);
	}
};

mutex timer_l;
priority_queue <EVENT_ST> timer_queue;

HANDLE g_iocp;
SOCKETINFO clients[MAX_USER + NUM_NPC];


void process_event(EVENT_ST &ev);
void send_chat_packet(int client, int from_id, wchar_t *mess);

void error_display(const char *mess, int err_no)
{
	WCHAR *lpMsgBuf;
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, err_no,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf, 0, NULL);
	cout << mess;
	wcout << L"에러 [" << err_no << L"]  " << lpMsgBuf << endl;
	while(true);
	LocalFree(lpMsgBuf);
}

bool is_NPC(int id)
{
	if ((id >= MAX_USER) && (id < MAX_USER + NUM_NPC))
		return true;
	else return false;
}

bool is_sleeping(int id)
{
	return clients[id].is_sleeping;
}

void Initialize_PC()
{
	for (int i = 0; i < MAX_USER; ++i) {
		clients[i].in_use = false;
		clients[i].is_login = false;
	}
}

int API_get_x(lua_State *L)
{
	int obj_id = (int)lua_tonumber(L, -1);
	lua_pop(L, 2);
	int x = clients[obj_id].x;
	lua_pushnumber(L, x);
	return 1;
}

int API_get_y(lua_State *L)
{
	int obj_id = (int)lua_tonumber(L, -1);
	lua_pop(L, 2);
	int y = clients[obj_id].y;
	lua_pushnumber(L, y);
	return 1;
}

int API_send_message(lua_State *L)
{
	int client_id = (int)lua_tonumber(L, -3);
	int from_id = (int)lua_tonumber(L, -2);
	char *mess = (char *)lua_tostring(L, -1);
	wchar_t wmess[MAX_STR_LEN];

	lua_pop(L, 4);

	size_t wlen, len;
	len = strnlen_s(mess, MAX_STR_LEN);
	mbstowcs_s(&wlen, wmess, len, mess, _TRUNCATE);

	send_chat_packet(client_id, from_id, wmess);
	return 0;
}

int API_move_NPC(lua_State *L) {
	int obj_id = (int)lua_tonumber(L, -3);
	char *state = (char *)lua_tostring(L, -2);
	int escape = (int)lua_tonumber(L, -1);
	lua_pop(L, 4);

	cout << state << ", " << escape << endl;

	if (strcmp(state, "s")) {
		lua_getglobal(L, "set_state");
		lua_pushstring(L, "escape");
		lua_pcall(L, 1, 0, 0);
	}

	EVENT_ST ev;
	ev.obj_id = obj_id;
	ev.type = EV_MOVE;
	ev.start_time = high_resolution_clock::now() + 1s;
	timer_l.lock();
	timer_queue.push(ev);
	timer_l.unlock();
	process_event(ev);

	if (escape > 0) {
		lua_getglobal(L, "set_escape_state");
		lua_pushnumber(L, escape - 1);
		lua_pcall(L, 1, 0, 0);
	}

	return 0;
}

int API_sleep_NPC(lua_State *L) {
	cout << "sleep NPC~~ \n";
	int obj_id = (int)lua_tonumber(L, -1);
	lua_pop(L, 2);

	if (false == is_sleeping(obj_id)) {
		clients[obj_id].is_sleeping = true;

	}

	lua_getglobal(L, "set_state");
	lua_pushstring(L, "sleep");
	lua_pcall(L, 1, 0, 0);

	lua_getglobal(L, "set_escape_state");
	lua_pushnumber(L, 3);
	lua_pcall(L, 1, 0, 0);

	return 1;
}

void add_timer(int obj_id, EVENT_TYPE et,
	high_resolution_clock::time_point start_time)
{
	timer_l.lock();
	timer_queue.emplace(EVENT_ST{ obj_id, et, start_time });
	timer_l.unlock();
}

void Initialize_NPC()
{
	for (int i = 0; i < NUM_NPC; ++i) {
		int npc_id = i + MAX_USER;
		clients[npc_id].in_use = true;
		clients[npc_id].is_sleeping = true;
		clients[npc_id].x = rand() % WORLD_WIDTH;
		clients[npc_id].y = rand() % WORLD_HEIGHT;
		add_timer(npc_id, EV_MOVE, high_resolution_clock::now() + 1s);

		lua_State *L = clients[npc_id].L;
		lua_getglobal(L, "set_uid");
		lua_pushnumber(L, npc_id);
		lua_pcall(L, 1, 0, 0);

		lua_getglobal(L, "set_state");
		lua_pushstring(L, "sleep");
		lua_pcall(L, 1, 0, 0);

		lua_getglobal(L, "set_escape_state");
		lua_pushnumber(L, 3);
		lua_pcall(L, 1, 0, 0);

		lua_register(L, "API_get_x", API_get_x);
		lua_register(L, "API_get_y", API_get_y);
		lua_register(L, "API_SendMessage", API_send_message);
		lua_register(L, "API_Move_NPC", API_move_NPC);
		lua_register(L, "API_Sleep_NPC", API_sleep_NPC);
	}
	wcout << L"몬스터 가상머신 초기화 완료";
}

bool Is_Near_Object(int a, int b)
{
	if (VIEW_RADIUS < abs(clients[a].x - clients[b].x)) 
		return false;
	if (VIEW_RADIUS < abs(clients[a].y - clients[b].y))
		return false;
	return true;
}

void wakeup_NPC(int id)
{
	if (true == is_sleeping(id)) {
		clients[id].is_sleeping = false;
		EVENT_ST ev;
		ev.obj_id = id;
		ev.type = EV_MOVE;
		ev.start_time = high_resolution_clock::now() + 1s;
		timer_l.lock();
		timer_queue.push(ev);
		timer_l.unlock();
	}
}

void do_recv(int id)
{
	DWORD flags = 0;

	if (WSARecv(clients[id].socket, &clients[id].over_ex.dataBuffer, 1,
		NULL, &flags, &(clients[id].over_ex.over), 0))
	{
		if (WSAGetLastError() != WSA_IO_PENDING)
		{
			cout << "Error - IO pending Failure\n";
			while (true);
		}
	}
	//else {
	//	cout << "Non Overlapped Recv return.\n";
	//	while (true);
	//}
}

void send_packet(int client, void *packet)
{
	char *p = reinterpret_cast<char *>(packet);
	OVER_EX *ov = new OVER_EX;
	ov->dataBuffer.len = p[0];
	ov->dataBuffer.buf = ov->messageBuffer;
	ov->event_t = EV_SEND;
	memcpy(ov->messageBuffer, p, p[0]);
	ZeroMemory(&ov->over, sizeof(ov->over));
	int error = WSASend(clients[client].socket, &ov->dataBuffer, 1, 0, 0,
		&ov->over, NULL);
	if (0 != error) {
		int err_no = WSAGetLastError();
		if (err_no != WSA_IO_PENDING)
		{
			cout << "Error - IO pending Failure\n";
			error_display("WSASend in send_packet()  ", err_no);
			while (true);
		}
	}
	else {
		//cout << "Non Overlapped Send return.\n";
		//while (true);
	}
}

void send_pos_packet(int client, int pl)
{
	sc_packet_move_player packet;
	packet.id = pl;
	packet.size = sizeof(packet);
	packet.type = SC_MOVE_PLAYER;
	packet.x = clients[pl].x;
	packet.y = clients[pl].y;

	send_packet(client, &packet);
}

void send_login_ok_packet(int new_id)
{
	sc_packet_login_ok packet;
	packet.id = new_id;
	packet.size = sizeof(packet);
	packet.type = SC_LOGIN_OK;

	send_packet(new_id, &packet);
}

void send_remove_player_packet(int cl, int id)
{
	sc_packet_remove_player packet;
	packet.id = id;
	packet.size = sizeof(packet);
	packet.type = SC_REMOVE_PLAYER;

	send_packet(cl, &packet);
}

void send_put_player_packet(int client, int new_id)
{
	sc_packet_put_player packet;
	packet.id = new_id;
	packet.size = sizeof(packet);
	packet.type = SC_PUT_PLAYER;
	packet.x = clients[new_id].x;
	packet.y = clients[new_id].y;

	send_packet(client, &packet);
}

void send_chat_packet(int client, int from_id, wchar_t *mess)
{
	sc_packet_chat packet;
	packet.id = from_id;
	packet.size = sizeof(packet);
	packet.type = SC_CHAT;
	wcscpy_s(packet.message, mess);
	send_packet(client, &packet);
}

void process_packet(int client, char *packet)
{
	if (!clients[client].is_login) {
		cs_packet_init *p = reinterpret_cast<cs_packet_init *>(packet);

		int new_id = client;

		if (CS_HOTSPOT_MOD == p->type) {
			clients[new_id].x = clients[new_id].y = 400;
		}
		else if (CS_CCU_MOD == p->type) {
			clients[new_id].x = rand() % WORLD_WIDTH;
			clients[new_id].y = rand() % WORLD_HEIGHT;
		}
		else {
			clients[new_id].x = rand() % 400;
			clients[new_id].y = rand() % 400;
		}

		clients[client].is_login = true;

		for (int i = 0; i < MAX_USER; ++i) {
			if (false == clients[i].in_use) continue;
			if (false == Is_Near_Object(new_id, i)) continue;
			if (i == new_id) continue;
			clients[i].v_lock.lock();
			clients[i].view_list.insert(new_id);
			clients[i].v_lock.unlock();

			send_put_player_packet(i, new_id);
		}

		for (int i = 0; i < MAX_USER + NUM_NPC; ++i) {
			if (false == clients[i].in_use) continue;
			if (i == new_id) continue;
			if (false == Is_Near_Object(i, new_id)) continue;
			if (true == is_NPC(i)) wakeup_NPC(i);
			clients[new_id].v_lock.lock();
			clients[new_id].view_list.insert(i);
			clients[new_id].v_lock.unlock();

			send_put_player_packet(new_id, i);
		}

	}
	else {
		cs_packet_up *p = reinterpret_cast<cs_packet_up *>(packet);

		int x = clients[client].x;
		int y = clients[client].y;

		clients[client].v_lock.lock();
		auto old_vl = clients[client].view_list;
		clients[client].v_lock.unlock();
		switch (p->type)
		{
		case CS_UP: if (y > 0) y--; break;
		case CS_DOWN: if (y < (WORLD_HEIGHT - 1)) y++; break;
		case CS_LEFT: if (x > 0) x--; break;
		case CS_RIGHT:if (x < (WORLD_WIDTH - 1)) x++; break;
		default:
			wcout << L"정의되지 않은 패킷 도착 오류!!\n";
			while (true);
		}
		clients[client].x = x;
		clients[client].y = y;

		unordered_set <int> new_vl;
		for (int i = 0; i < MAX_USER; ++i) {
			if (i == client) continue;
			if (false == clients[i].in_use) continue;
			if (false == Is_Near_Object(i, client)) continue;
			new_vl.insert(i);
		}

		for (int i = MAX_USER; i < MAX_USER + NUM_NPC; ++i) {
			if (false == Is_Near_Object(i, client)) continue;
			new_vl.insert(i);
		}

		send_pos_packet(client, client);

		// 1. old_vl에도 있고 new_vl에도 있는 객체
		for (auto pl : old_vl) {
			if (0 == new_vl.count(pl)) continue;
			if (true == is_NPC(pl)) continue;
			clients[pl].v_lock.lock();
			if (0 < clients[pl].view_list.count(client)) {
				clients[pl].v_lock.unlock();
				send_pos_packet(pl, client);
			}
			else {
				clients[pl].view_list.insert(client);
				clients[pl].v_lock.unlock();
				send_put_player_packet(pl, client);
			}
		}
		// 2. old_vl에 없고 new_vl에만 있는 플레이어
		for (auto pl : new_vl) {
			if (0 < old_vl.count(pl)) continue;
			clients[client].v_lock.lock();
			clients[client].view_list.insert(pl);
			clients[client].v_lock.unlock();

			send_put_player_packet(client, pl);
			if (true == is_NPC(pl)) {
				wakeup_NPC(pl);
				continue;
			}
			clients[pl].v_lock.lock();
			if (0 == clients[pl].view_list.count(client)) {
				clients[pl].view_list.insert(client);
				clients[pl].v_lock.unlock();

				send_put_player_packet(pl, client);
			}
			else {
				clients[pl].v_lock.unlock();
				send_pos_packet(pl, client);
			}
		}
		// 3. old_vl에 있고 new_vl에는 없는 플레이어
		for (auto pl : old_vl) {
			if (0 < new_vl.count(pl)) continue;
			clients[client].v_lock.lock();
			clients[client].view_list.erase(pl);
			clients[client].v_lock.unlock();

			send_remove_player_packet(client, pl);
			if (true == is_NPC(pl)) continue;

			clients[pl].v_lock.lock();
			if (0 < clients[pl].view_list.count(client)) {
				clients[pl].view_list.erase(client);
				clients[pl].v_lock.unlock();

				send_remove_player_packet(pl, client);
			}
			else 	clients[pl].v_lock.unlock();

		}

		for (auto monster : new_vl)
		{
			if (false == is_NPC(monster)) continue;
			OVER_EX *ex_over = new OVER_EX;
			ex_over->event_t = EV_PLAYER_MOVE_DETECT;
			ex_over->target_player = client;
			PostQueuedCompletionStatus(g_iocp, 1, monster, &ex_over->over);
		}
	}
}

void disconnect_client(int id)
{
	for (int i = 0; i < MAX_USER; ++i) {
		if (false == clients[i].in_use) continue;
		if (i == id) continue;
		clients[i].v_lock.lock();
		if (0 == clients[i].view_list.count(id)) {
			clients[i].v_lock.unlock();
			continue;
		}
		clients[i].view_list.erase(id);
		clients[i].v_lock.unlock();

		send_remove_player_packet(i, id);
	}
	closesocket(clients[id].socket);
	clients[id].in_use = false;
	clients[id].v_lock.lock();
	clients[id].view_list.clear();
	clients[id].v_lock.unlock();

}

void worker_thread()
{
	while (true) {
		DWORD io_byte;
		ULONGLONG l_key;
		OVER_EX *over_ex;
		int is_error = GetQueuedCompletionStatus(g_iocp, &io_byte,
			&l_key, reinterpret_cast<LPWSAOVERLAPPED *>(&over_ex), 
			INFINITE);
		int key = static_cast<int>(l_key);
		if (0 == is_error) {
			int err_no = WSAGetLastError();
			if (64 == err_no) {
				disconnect_client(key);
				continue;
			}
			else error_display("GQCS : ", err_no);
		}

		if (0 == io_byte) {
			disconnect_client(key);
			continue;
		}

		if (EV_RECV == over_ex->event_t) {
			// wcout << "Packet from Client:" << key << endl;
			// 패킷조립
			int rest = io_byte;
			char *ptr = over_ex->messageBuffer;
			char packet_size = 0;
			if (0 < clients[key].prev_size)
				packet_size = clients[key].packet_buffer[0];
			while (0 < rest) {
				if (0 == packet_size) packet_size = ptr[0];
				int required = packet_size - clients[key].prev_size;
				if (required <= rest) {
					memcpy(clients[key].packet_buffer + clients[key].prev_size,
						ptr, required);
					process_packet(key, clients[key].packet_buffer);
					rest -= required;
					ptr += required;
					packet_size = 0;
					clients[key].prev_size = 0;
				}
				else {
					memcpy(clients[key].packet_buffer + clients[key].prev_size,
						ptr, rest);
					rest = 0;
					clients[key].prev_size += rest;
				}
			}
			do_recv(key);
		}
		else if (EV_SEND == over_ex->event_t) {
			delete over_ex;
		}
		else if (EV_MOVE == over_ex->event_t) {
			lua_State *L = clients[key].L;
			int player_id = over_ex->target_player;

			clients[key].l_lock.lock();
			lua_getglobal(L, "event_npc_move");
			lua_pushnumber(L, player_id);
			lua_pcall(L, 1, 0, 0);
			clients[key].l_lock.unlock();

			//EVENT_ST ev;
			//ev.obj_id = key;
			//ev.start_time = high_resolution_clock::now();
			//ev.type = EV_MOVE;
			//process_event(ev);

			delete over_ex;
		}
		else if (EV_PLAYER_MOVE_DETECT == over_ex->event_t) {
			lua_State *L = clients[key].L;
			int player_id = over_ex->target_player;

			clients[key].l_lock.lock();
			lua_getglobal(L, "event_player_move");
			lua_pushnumber(L, player_id);
			lua_pcall(L, 1, 0, 0);
			clients[key].l_lock.unlock();


			delete over_ex;
		}
		else
		{
			cout << "UNKNOWN EVENT\n";
			while (true);
		}
	}
}

int do_accept()
{
	// Winsock Start - windock.dll 로드
	WSADATA WSAData;
	if (WSAStartup(MAKEWORD(2, 2), &WSAData) != 0)
	{
		cout << "Error - Can not load 'winsock.dll' file\n";
		return 1;
	}

	// 1. 소켓생성  
	SOCKET listenSocket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (listenSocket == INVALID_SOCKET)
	{
		cout << "Error - Invalid socket\n";
		return 1;
	}

	// 서버정보 객체설정
	SOCKADDR_IN serverAddr;
	memset(&serverAddr, 0, sizeof(SOCKADDR_IN));
	serverAddr.sin_family = PF_INET;
	serverAddr.sin_port = htons(SERVER_PORT);
	serverAddr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);

	// 2. 소켓설정
	if (::bind(listenSocket, (struct sockaddr*)&serverAddr, 
		sizeof(SOCKADDR_IN)) == SOCKET_ERROR)
	{
		cout << "Error - Fail bind\n";
		// 6. 소켓종료
		closesocket(listenSocket);
		// Winsock End
		WSACleanup();
		return 1;
	}

	// 3. 수신대기열생성
	if (listen(listenSocket, 5) == SOCKET_ERROR)
	{
		cout << "Error - Fail listen\n";
		// 6. 소켓종료
		closesocket(listenSocket);
		// Winsock End
		WSACleanup();
		return 1;
	}

	SOCKADDR_IN clientAddr;
	int addrLen = sizeof(SOCKADDR_IN);
	memset(&clientAddr, 0, addrLen);
	SOCKET clientSocket;
	DWORD flags;

	while (1)
	{
		clientSocket = accept(listenSocket, (struct sockaddr *)&clientAddr, &addrLen);
		if (clientSocket == INVALID_SOCKET)
		{
			cout << "Error - Accept Failure\n";
			return 1;
		}

		int new_id = -1;
		for (int i = 0; i < MAX_USER; ++i)
			if (false == clients[i].in_use) {
				new_id = i;
				break;
			}
		if (-1 == new_id) {
			cout << "MAX USER overflow\n";
			continue;
		}

		clients[new_id].socket = clientSocket;
		clients[new_id].prev_size = 0;
		clients[new_id].v_lock.lock();
		clients[new_id].view_list.clear();
		clients[new_id].v_lock.unlock();

		ZeroMemory(&clients[new_id].over_ex.over,
			sizeof(clients[new_id].over_ex.over));
		flags = 0;

		CreateIoCompletionPort(reinterpret_cast<HANDLE>(clientSocket),
			g_iocp, new_id, 0);

		clients[new_id].in_use = true;

		send_login_ok_packet(new_id);
		send_put_player_packet(new_id, new_id);

		do_recv(new_id);

	}

	// 6-2. 리슨 소켓종료
	closesocket(listenSocket);

	// Winsock End
	WSACleanup();

	return 0;
}

void random_move_NPC(int id)
{
	int x = clients[id].x;
	int y = clients[id].y;
	unordered_set <int> old_vl;
	for (int i = 0; i < MAX_USER; ++i) {
		if (false == clients[i].in_use) continue;
		if (false == Is_Near_Object(id, i)) continue;
		old_vl.insert(i);
	}
	switch (rand() % 4) {
	case 0: if (x > 0) x--; break;
	case 1: if (x < (WORLD_WIDTH - 1)) x++; break;
	case 2: if (y > 0) y--; break;
	case 3: if (y < (WORLD_HEIGHT - 1)) y++; break;
	}
	clients[id].x = x;
	clients[id].y = y;
	unordered_set <int> new_vl;
	for (int i = 0; i < MAX_USER; ++i) {
		if (false == clients[i].in_use) continue;
		if (false == Is_Near_Object(id, i)) continue;
		new_vl.insert(i);
	}
	// 새로 만난 플레이어, 계속 보는 플레이어 처리
	for (auto pl : new_vl) {
		clients[pl].v_lock.lock();
		if (0 == clients[pl].view_list.count(pl)) {
			clients[pl].view_list.insert(id);
			clients[pl].v_lock.unlock();

			send_put_player_packet(pl, id);
		}
		else {
			clients[pl].v_lock.unlock();
			send_pos_packet(pl, id);
		}
	}
	// 헤어진 플레이어 처리
	for (auto pl : old_vl) {
		if (0 == new_vl.count(pl)) {
			clients[pl].v_lock.lock();
			if (0 < clients[pl].view_list.count(pl)) {
				clients[pl].view_list.erase(id);
				clients[pl].v_lock.unlock();

				send_remove_player_packet(pl, id);
			} else 	clients[pl].v_lock.unlock();

		}
	}

	for (auto pl : new_vl)
	{
		if (true == is_NPC(pl)) continue;

		OVER_EX *ex_over = new OVER_EX;
		ex_over->event_t = EV_MOVE;
		ex_over->target_player = pl;
		PostQueuedCompletionStatus(g_iocp, 1, id, &ex_over->over);
	}

}

void do_AI()
{
	while (true) {
		this_thread::sleep_for(1s);
		auto ai_start_t = high_resolution_clock::now();
		for (int i = MAX_USER; i < MAX_USER + NUM_NPC; ++i) {
			bool need_move = false;
			for (int j = 0; j < MAX_USER; ++j) {
				if (false == clients[j].in_use) continue;
				if (false == Is_Near_Object(i, j)) continue;
				need_move = true;
			}
			if (need_move) random_move_NPC(i);
		}
		auto ai_end_t = high_resolution_clock::now();
		auto ai_time = ai_end_t - ai_start_t;
		cout << "AI Processing time = ";
		cout << duration_cast<milliseconds>(ai_time).count();
		cout << "ms\n";
	}
}

void process_event(EVENT_ST &ev)
{
	switch (ev.type) {
	case EV_MOVE: {
		bool player_is_near = false;
		for (int i = 0; i < MAX_USER; ++i) {
			if (false == clients[i].in_use) continue;
			if (false == Is_Near_Object(i, ev.obj_id)) continue;
			player_is_near = true;
			break;
		}
		random_move_NPC(ev.obj_id);
		//add_timer(ev.obj_id, EV_MOVE,
		//	high_resolution_clock::now() + 1s);

		//if (player_is_near) {
		//	random_move_NPC(ev.obj_id);
		//	add_timer(ev.obj_id, EV_MOVE, 
		//		high_resolution_clock::now() + 1s);
		//}
		//else {
		//	clients[ev.obj_id].is_sleeping = true;
		//}
		break;
	}
	default:
		cout << "Unknown Event Error!\n";
		while (true);
	}
}

void do_timer()
{
	while (true) {
		this_thread::sleep_for(10ms);
		while (true) {
			timer_l.lock();
			if (true == timer_queue.empty()) {
				timer_l.unlock();
				break;
			}
			EVENT_ST ev = timer_queue.top();

			if (ev.start_time > high_resolution_clock::now()) {
				timer_l.unlock();
				break;
			}
			timer_queue.pop();
			timer_l.unlock();
			OVER_EX *over_ex = new OVER_EX;
			over_ex->event_t = EV_MOVE;
			PostQueuedCompletionStatus(g_iocp, 1, ev.obj_id, &over_ex->over);
			// process_event(ev);
		}
	}
}

int main()
{
	vector <thread> worker_threads;

	wcout.imbue(locale("korean"));

	Initialize_PC();
	Initialize_NPC();
	g_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);
	for (int i = 0; i < 4; ++i)
		worker_threads.emplace_back(thread{ worker_thread });
	thread accept_thread{ do_accept };
	//thread AI_thread{ do_AI };
	//AI_thread.join();
	thread timer_thread{ do_timer };
	timer_thread.join();
	accept_thread.join();
	for (auto &th : worker_threads) th.join();
}