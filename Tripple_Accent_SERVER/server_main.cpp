#include <iostream>
#include <unordered_set>
#include <vector>
#include <thread>
#include <queue>
#include <mutex>
using namespace std;
using namespace chrono;
#include <locale.h>
#include <winsock2.h>

#include "2019_텀프_protocol.h"
#include "server_h.h"

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "lua53.lib")

#define MAX_BUFFER        1024

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
	bool is_dummy;
	bool in_use;
	OVER_EX over_ex;
	SOCKET socket;
	char packet_buffer[MAX_BUFFER];
	int	prev_size;

	bool is_login;

	char  login_id[MAX_ID_LEN];
	char  login_kind, kind;
	short x, y;
	unsigned short HP, LEVEL, ATTACK;
	int   EXP, GOLD;
	short item[NUM_ITEM];

	unordered_set <int> view_list;
	unordered_set <int> npc_view_list;

	mutex v_lock;

	SOCKETINFO() {
		in_use = false;
		over_ex.dataBuffer.len = MAX_BUFFER;
		over_ex.dataBuffer.buf = over_ex.messageBuffer;
		over_ex.event_t = EV_RECV;
	}
};

class NPCINFO {
public:
	int target_user_id;

	bool is_sleeping;
	bool is_die;

	char kind, type, move_type;
	unsigned short x, y;
	unsigned short LEVEL, HP, ATTACK, GOLD, EXP;

	mutex l_lock;

	lua_State *L;
	NPCINFO() {
		L = luaL_newstate();
		luaL_openlibs(L);

		luaL_loadfile(L, "monster_ai.lua");
		lua_pcall(L, 0, 0, 0);
	}
};

class ITEMINFO {
	char name;
	char price;
};


mutex timer_lock;
mutex db_lock;
priority_queue <EVENT_ST> timer_queue;
queue<DB_EVENT_ST> db_queue;

HANDLE g_iocp;
SOCKETINFO clients[MAX_USER];
NPCINFO npcs[NUM_NPC];



// function
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
	while (true);
	LocalFree(lpMsgBuf);
}
void db_err_display(SQLHANDLE hHandle, SQLSMALLINT hType, RETCODE RetCode)
{
	SQLSMALLINT iRec = 0;
	SQLINTEGER iError;
	WCHAR wszMessage[1000];
	WCHAR wszState[SQL_SQLSTATE_SIZE + 1];
	if (RetCode == SQL_INVALID_HANDLE) {
		fwprintf(stderr, L"Invalid handle!\n");
		return;
	}
	while (SQLGetDiagRec(hType, hHandle, ++iRec, wszState, &iError, wszMessage,
		(SQLSMALLINT)(sizeof(wszMessage) / sizeof(WCHAR)), (SQLSMALLINT *)NULL) == SQL_SUCCESS) {
		// Hide data truncated..
		if (wcsncmp(wszState, L"01004", 5)) {
			fwprintf(stderr, L"[%5.5s] %s (%d)\n", wszState, wszMessage, iError);
		}
	}
}

void Initialize_PC()
{
	for (int i = 0; i < MAX_USER; ++i) {
		clients[i].in_use = false;
		clients[i].is_login = false;
		clients[i].is_dummy = false;
	}
}
void Initialize_NPC()
{
	wcout << L"몬스터 가상머신 초기화 시작\n";
	int space = 5;

	for (int npc_id = 0; npc_id < NUM_NPC; ++npc_id) {
		if (npc_id < 1500) {
			npcs[npc_id].kind = FAIRY;
			npcs[npc_id].type = PEACE;
			npcs[npc_id].move_type = NO_MOVE;
			npcs[npc_id].LEVEL = 5;
			npcs[npc_id].EXP = 25;
			npcs[npc_id].GOLD = 2;
			npcs[npc_id].HP = 10;
			npcs[npc_id].ATTACK = 10;

			if(rand() % 2 == 0) npcs[npc_id].x = rand() % 55;
			else npcs[npc_id].x = rand() % 55 + 245;
			
			npcs[npc_id].y = rand() % 290 + 5;
		}
		else if (npc_id >= 1500 && npc_id < 3000) {
			npcs[npc_id].kind = DEVIL;

			npcs[npc_id].type = WAR;
			npcs[npc_id].move_type = YES_MOVE;
			npcs[npc_id].LEVEL = 3;
			npcs[npc_id].EXP = 60;
			npcs[npc_id].GOLD = 1;
			npcs[npc_id].HP = 20;
			npcs[npc_id].ATTACK = 25;

			npcs[npc_id].x = rand() % 290 + 5;
			npcs[npc_id].y = rand() % 55 + 245;

		}
		else if (npc_id >= 3000 && npc_id < 4500) {
			npcs[npc_id].kind = ANGEL;

			npcs[npc_id].type = WAR;
			npcs[npc_id].move_type = YES_MOVE;
			npcs[npc_id].LEVEL = 4;
			npcs[npc_id].EXP = 80;
			npcs[npc_id].GOLD = 3;
			npcs[npc_id].HP = 60;
			npcs[npc_id].ATTACK = 30;

			npcs[npc_id].x = rand() % 290 + 5;
			npcs[npc_id].y = rand() % 55;

		}

		else if (npc_id >= 4500 && npc_id < 5250) {
			npcs[npc_id].kind = FAIRY;
			npcs[npc_id].type = WAR;
			npcs[npc_id].move_type = NO_MOVE;
			npcs[npc_id].LEVEL = 50;
			npcs[npc_id].EXP = 500;
			npcs[npc_id].GOLD = 30;
			npcs[npc_id].HP = 450;
			npcs[npc_id].ATTACK = 210;

			if (rand() % 2 == 0) npcs[npc_id].x = rand() % 45 + 55;
			else npcs[npc_id].x = rand() % 45 + 200;

			npcs[npc_id].y = rand() % 190 + 55;
		}
		else if (npc_id >= 5250 && npc_id < 6000) {
			npcs[npc_id].kind = DEVIL;

			npcs[npc_id].type = PEACE;
			npcs[npc_id].move_type = NO_MOVE;
			npcs[npc_id].LEVEL = 45;
			npcs[npc_id].EXP = 225;
			npcs[npc_id].GOLD = 70;
			npcs[npc_id].HP = 280;
			npcs[npc_id].ATTACK = 150;

			npcs[npc_id].x = rand() % 190 + 55;
			npcs[npc_id].y = rand() % 45 + 195;

		}
		else if (npc_id >= 6000 && npc_id < 6750) {
			npcs[npc_id].kind = ANGEL;

			npcs[npc_id].type = PEACE;
			npcs[npc_id].move_type = NO_MOVE;
			npcs[npc_id].LEVEL = 20;
			npcs[npc_id].EXP = 100;
			npcs[npc_id].GOLD = 10;
			npcs[npc_id].HP = 110;
			npcs[npc_id].ATTACK = 135;

			npcs[npc_id].x = rand() % 190 + 55;
			npcs[npc_id].y = rand() % 45 + 55;

		}

		else if (npc_id >= 6750 && npc_id < 7050) {
			npcs[npc_id].kind = FAIRY;
			npcs[npc_id].type = WAR;
			npcs[npc_id].move_type = YES_MOVE;
			npcs[npc_id].LEVEL = 105;
			npcs[npc_id].EXP = 2100;
			npcs[npc_id].GOLD = 450;
			npcs[npc_id].HP = 1850;
			npcs[npc_id].ATTACK = 350;

			if (rand() % 2 == 0) npcs[npc_id].x = rand() % 35 + 110;
			else npcs[npc_id].x = rand() % 35 + 160;

			npcs[npc_id].y = rand() % 100 + 100;
		}
		else if (npc_id >= 7050 && npc_id < 7050) {
			npcs[npc_id].kind = DEVIL;

			npcs[npc_id].type = PEACE;
			npcs[npc_id].move_type = YES_MOVE;
			npcs[npc_id].LEVEL = 80;
			npcs[npc_id].EXP = 800;
			npcs[npc_id].GOLD = 180;
			npcs[npc_id].HP = 900;
			npcs[npc_id].ATTACK = 270;

			npcs[npc_id].x = rand() % 90 + 100;
			npcs[npc_id].y = rand() % 35 + 160;

		}
		else if (npc_id >= 7350 && npc_id < 7650) {
			npcs[npc_id].kind = ANGEL;

			npcs[npc_id].type = WAR;
			npcs[npc_id].move_type = YES_MOVE;
			npcs[npc_id].LEVEL = 160;
			npcs[npc_id].EXP = 3200;
			npcs[npc_id].GOLD = 720;
			npcs[npc_id].HP = 3350;
			npcs[npc_id].ATTACK = 480;

			npcs[npc_id].x = rand() % 90 + 100;
			npcs[npc_id].y = rand() % 35 + 110;

		}
		
		else if (npc_id == 7650) {
			npcs[npc_id].kind = DRAGON;
			npcs[npc_id].type = WAR;
			npcs[npc_id].move_type = YES_MOVE;
			npcs[npc_id].LEVEL = 330;
			npcs[npc_id].EXP = 6600;
			npcs[npc_id].GOLD = 3200;
			npcs[npc_id].HP = 65535;
			npcs[npc_id].ATTACK = 250;
			npcs[npc_id].x = 150;
			npcs[npc_id].y = 150;
		}
		else if (npc_id == 7651) {
			npcs[npc_id].kind = FAIRY;
			npcs[npc_id].type = PEACE;
			npcs[npc_id].move_type = NO_MOVE;
			npcs[npc_id].LEVEL = 300;
			npcs[npc_id].EXP = 0;
			npcs[npc_id].GOLD = 0;
			npcs[npc_id].HP = 65535;
			npcs[npc_id].ATTACK = 0;
			npcs[npc_id].x = 65;
			npcs[npc_id].y = 150;

		}
		else if (npc_id == 7652) {
			npcs[npc_id].kind = DEVIL;
			npcs[npc_id].type = PEACE;
			npcs[npc_id].move_type = NO_MOVE;
			npcs[npc_id].LEVEL = 300;
			npcs[npc_id].EXP = 0;
			npcs[npc_id].GOLD = 0;
			npcs[npc_id].HP = 65535;
			npcs[npc_id].ATTACK = 0;
			npcs[npc_id].x = 150;
			npcs[npc_id].y = 235;
		}
		else if (npc_id == 7653) {
			npcs[npc_id].kind = ANGEL;
			npcs[npc_id].type = PEACE;
			npcs[npc_id].move_type = NO_MOVE;
			npcs[npc_id].LEVEL = 300;
			npcs[npc_id].EXP = 0;
			npcs[npc_id].GOLD = 0;
			npcs[npc_id].HP = 65535;
			npcs[npc_id].ATTACK = 0;
			npcs[npc_id].x = 150;
			npcs[npc_id].y = 65;
		}
	}

	for (int npc_id = 0; npc_id < NUM_NPC; ++npc_id) {
		npcs[npc_id].target_user_id = -1;

		npcs[npc_id].is_sleeping = true;
		npcs[npc_id].is_die = false;

		if(npcs[npc_id].move_type == 2)	add_timer(npc_id, EV_MOVE, high_resolution_clock::now() + 1s);

		lua_State *L = npcs[npc_id].L;
		lua_getglobal(L, "set_npc_info");
		lua_pushnumber(L, npc_id);
		lua_pushnumber(L, npcs[npc_id].kind);
		lua_pushnumber(L, npcs[npc_id].type);
		lua_pushnumber(L, npcs[npc_id].move_type);
		lua_pushnumber(L, npcs[npc_id].x);
		lua_pushnumber(L, npcs[npc_id].y);
		lua_pushnumber(L, npcs[npc_id].LEVEL);
		lua_pushnumber(L, npcs[npc_id].EXP);
		lua_pushnumber(L, npcs[npc_id].GOLD);
		lua_pushnumber(L, npcs[npc_id].HP);
		lua_pushnumber(L, npcs[npc_id].ATTACK);
		lua_pcall(L, 11, 0, 0);

		lua_register(L, "API_load_NPC_info", API_load_NPC_info);

		lua_register(L, "API_get_player_x", API_get_player_x);
		lua_register(L, "API_get_player_y", API_get_player_y);
		lua_register(L, "API_get_npc_x", API_get_npc_x);
		lua_register(L, "API_get_npc_y", API_get_npc_y);

		lua_register(L, "API_SendMessage", API_send_message);
	}
	wcout << L"몬스터 가상머신 초기화 완료";
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
		else if (DB_EVT_SEARCH == over_ex->event_t) {
			if (!clients[key].is_dummy) {
				DB_EVENT_ST dev;
				dev.client_id = key;
				dev.type = DB_EVT_SEARCH;

				process_db_event(dev);
			}
			delete over_ex;
		}
		else if (DB_EVT_SAVE == over_ex->event_t) {
			if (!clients[key].is_dummy) {
				DB_EVENT_ST dev;
				dev.client_id = key;
				dev.type = DB_EVT_SAVE;

				process_db_event(dev);
			}
			delete over_ex;
		}
		else if (DB_EVT_UPDATE == over_ex->event_t) {
			DB_EVENT_ST dev;
			dev.client_id = key;
			dev.type = DB_EVT_UPDATE;

			process_db_event(dev);
			delete over_ex;
		}

		else if (EV_ATTACK == over_ex->event_t) {
			EVENT_ST ev;
			ev.obj_id = key;
			ev.start_time = high_resolution_clock::now();
			ev.type = EV_ATTACK;
			process_event(ev);

			delete over_ex;
		}
		else if (EV_NPC_ATTACK == over_ex->event_t) {
			EVENT_ST ev;
			ev.obj_id = key;
			ev.start_time = high_resolution_clock::now();
			ev.type = EV_NPC_ATTACK;
			process_event(ev);

			delete over_ex;
		}
		else if (EV_MOVE == over_ex->event_t) {
			EVENT_ST ev;
			ev.obj_id = key;
			ev.start_time = high_resolution_clock::now();
			ev.type = EV_MOVE;
			process_event(ev);

			delete over_ex;
		}
		else if (EV_RESURRECTION == over_ex->event_t) {
			EVENT_ST ev;
			ev.obj_id = key;
			ev.start_time = high_resolution_clock::now() + 1s;
			ev.type = EV_RESURRECTION;
			process_event(ev);

			delete over_ex;
		}
		else if (EV_NPC_RESURRECTION == over_ex->event_t) {
			EVENT_ST ev;
			ev.obj_id = key;
			ev.start_time = high_resolution_clock::now();
			ev.type = EV_NPC_RESURRECTION;
			process_event(ev);

			delete over_ex;
		}
		else if (EV_PLAYER_MOVE_DETECT == over_ex->event_t) {
			if (npcs[key].type == WAR &&
				npcs[key].target_user_id == -1) 
			{
				int player_id = over_ex->target_player;

				npcs[key].target_user_id = player_id;
			}

			delete over_ex;
		}
		else
		{
			cout << "UNKNOWN EVENT\n";
			while (true);
		}
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
		for (int i = 0; i < MAX_USER; ++i) {
			if (false == clients[i].in_use) {
				new_id = i;
				break;
			}
		}

		if (-1 == new_id) {
			cout << "MAX USER overflow\n";
			continue;
		}

		clients[new_id].socket = clientSocket;
		clients[new_id].prev_size = 0;
		clients[new_id].v_lock.lock();
		clients[new_id].view_list.clear();
		clients[new_id].npc_view_list.clear();
		clients[new_id].v_lock.unlock();
		ZeroMemory(&clients[new_id].item,
			sizeof(clients[new_id].item));

		ZeroMemory(&clients[new_id].over_ex.over,
			sizeof(clients[new_id].over_ex.over));

		flags = 0;

		CreateIoCompletionPort(reinterpret_cast<HANDLE>(clientSocket),
			g_iocp, new_id, 0);

		clients[new_id].in_use = true;

		do_recv(new_id);

	}

	// 6-2. 리슨 소켓종료
	closesocket(listenSocket);

	// Winsock End
	WSACleanup();

	return 0;
}
void do_timer()
{
	while (true) {
		this_thread::sleep_for(10ms);
		while (true) {
			timer_lock.lock();
			if (true == timer_queue.empty()) {
				timer_lock.unlock();
				break;
			}
			EVENT_ST ev = timer_queue.top();

			if (ev.start_time > high_resolution_clock::now()) {
				timer_lock.unlock();
				break;
			}
			timer_queue.pop();
			timer_lock.unlock();


			OVER_EX *over_ex = new OVER_EX;
			over_ex->event_t = ev.type;
			PostQueuedCompletionStatus(g_iocp, 1, ev.obj_id, &over_ex->over);
			// process_event(ev);
		}
	}
}
void do_transaction() {
	while (true) {
		this_thread::sleep_for(10ms);
		while (true) {
			db_lock.lock();
			if (true == db_queue.empty()) {
				db_lock.unlock();
				break;
			}

			DB_EVENT_ST ev = db_queue.front();

			db_queue.pop();
			db_lock.unlock();

			OVER_EX *over_ex = new OVER_EX;
			over_ex->event_t = ev.type;
			PostQueuedCompletionStatus(g_iocp, 1, ev.client_id, &over_ex->over);
		}
	}
}
void disconnect_client(int id)
{
	if (clients[id].is_login) add_db_evt(id, DB_EVT_SAVE);

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
	clients[id].npc_view_list.clear();
	clients[id].v_lock.unlock();

}

void add_timer(int obj_id, EVENT_TYPE et, high_resolution_clock::time_point start_time)
{
	timer_lock.lock();
	timer_queue.emplace(EVENT_ST{ obj_id, et, start_time });
	timer_lock.unlock();
}
void add_db_evt(int client_id, EVENT_TYPE et) {
	db_lock.lock();
	db_queue.emplace(DB_EVENT_ST{ client_id, et });
	db_lock.unlock();
}

bool search_user_id(int client) {
	SQLHENV henv;
	SQLHDBC hdbc;
	SQLHSTMT hstmt = 0;
	SQLRETURN retcode;

	SQLWCHAR uid[20];
	SQLINTEGER uposx, uposy, ukind, ulevel, uhp, uattack, uexp, ugold;
	SQLLEN cb_uid = 0, cb_uposx = 0, cb_uposy = 0, cb_ukind = 0, cb_ulevel = 0, 
		cb_uhp = 0, cb_uattack = 0, cb_uexp = 0, cb_ugold = 0;
	wchar_t buf[128] = {};

	setlocale(LC_ALL, "korean");

	// Allocate environment handle  
	retcode = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv);

	// Set the ODBC version environment attribute  
	if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
		retcode = SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER*)SQL_OV_ODBC3, 0);

		// Allocate connection handle  
		if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
			retcode = SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc);

			// Set login timeout to 5 seconds  
			if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
				SQLSetConnectAttr(hdbc, SQL_LOGIN_TIMEOUT, (SQLPOINTER)5, 0);

				// Connect to data source  
				retcode = SQLConnect(hdbc, (SQLWCHAR*)L"TRIPPLE_ACCENT", SQL_NTS, (SQLWCHAR*)NULL, 0, NULL, 0);

				// Allocate statement handle  
				if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
					SQLCloseCursor(hstmt);
					SQLFreeStmt(hstmt, SQL_UNBIND);

					cout << "SQL DB Connect OK!!\n";

					retcode = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);


					char query[128] = "EXEC select_user_id ";
					strncat_s(query, clients[client].login_id, MAX_ID_LEN);
										cout << "strcat 결과 : " << query << endl;

					size_t wlen, len;
					len = strnlen_s(query, 128);
					mbstowcs_s(&wlen, buf, len + 1, query, len);


					retcode = SQLExecDirect(hstmt, (SQLWCHAR *)buf, SQL_NTS);

					if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {

						// Bind columns 1, 2, and 3  
						//retcode = SQLBindCol(hstmt, 1, SQL_INTEGER, &uid, 100, &cb_uid);
						retcode = SQLBindCol(hstmt, 1, SQL_C_CHAR, uid, 20, &cb_uid);
						retcode = SQLBindCol(hstmt, 2, SQL_INTEGER, &ukind, 10, &cb_ukind);
						retcode = SQLBindCol(hstmt, 3, SQL_INTEGER, &ugold, 10, &cb_ugold);
						retcode = SQLBindCol(hstmt, 4, SQL_INTEGER, &ulevel, 10, &cb_ulevel);
						retcode = SQLBindCol(hstmt, 5, SQL_INTEGER, &uexp, 10, &cb_uexp);
						retcode = SQLBindCol(hstmt, 6, SQL_INTEGER, &uhp, 10, &cb_uhp);
						retcode = SQLBindCol(hstmt, 7, SQL_INTEGER, &uattack, 10, &cb_uattack);
						retcode = SQLBindCol(hstmt, 8, SQL_INTEGER, &uposx, 10, &cb_uposx);
						retcode = SQLBindCol(hstmt, 9, SQL_INTEGER, &uposy, 10, &cb_uposy);

						retcode = SQLFetch(hstmt);
						if (retcode == SQL_ERROR || retcode == SQL_SUCCESS_WITH_INFO)
							printf("error\n");
						if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
							clients[client].kind = ukind;
							if (clients[client].login_kind != ukind) return false;
							wprintf(L"%S : %d %d\n", uid, uposx, uposy);
							clients[client].x = uposx;
							clients[client].y = uposy;
							clients[client].GOLD = ugold;
							clients[client].LEVEL = ulevel;
							clients[client].EXP = uexp;
							clients[client].HP = uhp;
							clients[client].ATTACK = uattack;
							clients[client].is_login = true;
							return true;
						}
						else {
							SQLCloseCursor(hstmt);
							SQLFreeStmt(hstmt, SQL_UNBIND);

							wprintf(L"No Search ID!! \n");
							wprintf(L"Create New ID!! \n");

							char query2[256] = {};
							char query_buf[30] = "EXEC insert_user_data ";
							char kind_buf[10] = {};
							char pos_x_buf[10] = {};
							char pos_y_buf[10] = {};
							wchar_t buf2[256] = {};

							if (clients[client].login_kind == FAIRY) {
								clients[client].x = 65;
								clients[client].y = 150;
							}
							else if (clients[client].login_kind == DEVIL) {
								clients[client].x = 150;
								clients[client].y = 235;
							}
							else if (clients[client].login_kind == ANGEL) {
								clients[client].x = 150;
								clients[client].y = 65;
							}

							_itoa_s(clients[client].login_kind, kind_buf, 10);
							_itoa_s(clients[client].x, pos_x_buf, 10);
							_itoa_s(clients[client].y, pos_y_buf, 10);

							sprintf_s(query2, "%s%s, %s, %s, %s", query_buf, clients[client].login_id,
								kind_buf, pos_x_buf, pos_y_buf);

							cout << "sprintf_s 결과 : " << query2 << endl;

							size_t wlen2 = 0, len2 = 0;
							len2 = strnlen_s(query2, 256);
							mbstowcs_s(&wlen2, buf2, len2 + 1, query2, len2);

							retcode = SQLExecDirect(hstmt, (SQLWCHAR *)buf2, SQL_NTS);
							if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {

								// Bind columns 1, 2, and 3  
								//retcode = SQLBindCol(hstmt, 1, SQL_INTEGER, &uid, 100, &cb_uid);
								retcode = SQLBindCol(hstmt, 1, SQL_C_CHAR, uid, 20, &cb_uid);
								retcode = SQLBindCol(hstmt, 2, SQL_INTEGER, &ukind, 10, &cb_ukind);
								retcode = SQLBindCol(hstmt, 3, SQL_INTEGER, &ugold, 10, &cb_ugold);
								retcode = SQLBindCol(hstmt, 4, SQL_INTEGER, &ulevel, 10, &cb_ulevel);
								retcode = SQLBindCol(hstmt, 5, SQL_INTEGER, &uexp, 10, &cb_uexp);
								retcode = SQLBindCol(hstmt, 6, SQL_INTEGER, &uhp, 10, &cb_uhp);
								retcode = SQLBindCol(hstmt, 7, SQL_INTEGER, &uattack, 10, &cb_uattack);
								retcode = SQLBindCol(hstmt, 8, SQL_INTEGER, &uposx, 10, &cb_uposx);
								retcode = SQLBindCol(hstmt, 9, SQL_INTEGER, &uposy, 10, &cb_uposy);

								retcode = SQLFetch(hstmt);
								if (retcode == SQL_ERROR || retcode == SQL_SUCCESS_WITH_INFO)
									printf("error\n");
								if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
									wprintf(L"%S : %d %d\n", uid, uposx, uposy);
									clients[client].x = uposx;
									clients[client].y = uposy;
									clients[client].kind = ukind;
									clients[client].GOLD = ugold;
									clients[client].LEVEL = ulevel;
									clients[client].EXP = uexp;
									clients[client].HP = uhp;
									clients[client].ATTACK = uattack;
									clients[client].is_login = true;
									return true;
								}
								else {
									wcout << L"ID 생성 실패!! \n";
									return false;
								}
							}
							else {
								db_err_display(hstmt, SQL_HANDLE_STMT, retcode);
							}
						}
					}
					else {
						db_err_display(hstmt, SQL_HANDLE_STMT, retcode);
					}

					// Process data  
					if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
						SQLCancel(hstmt);
						SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
					}

					SQLDisconnect(hdbc);
				}

				SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
			}
		}
		SQLFreeHandle(SQL_HANDLE_ENV, henv);
	}
}
bool save_user_data(int client) {
	SQLHENV henv;
	SQLHDBC hdbc;
	SQLHSTMT hstmt = 0;
	SQLRETURN retcode;

	SQLWCHAR uid[20];
	SQLINTEGER uposx, uposy, ukind, ulevel, uhp, uattack, uexp, ugold;
	SQLLEN cb_uid = 0, cb_uposx = 0, cb_uposy = 0, cb_ukind = 0, cb_ulevel = 0,
		cb_uhp = 0, cb_uattack = 0, cb_uexp = 0, cb_ugold = 0;

	setlocale(LC_ALL, "korean");

	// Allocate environment handle  
	retcode = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv);

	// Set the ODBC version environment attribute  
	if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
		retcode = SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER*)SQL_OV_ODBC3, 0);

		// Allocate connection handle  
		if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
			retcode = SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc);

			// Set login timeout to 5 seconds  
			if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
				SQLSetConnectAttr(hdbc, SQL_LOGIN_TIMEOUT, (SQLPOINTER)5, 0);

				// Connect to data source  
				retcode = SQLConnect(hdbc, (SQLWCHAR*)L"TRIPPLE_ACCENT", SQL_NTS, (SQLWCHAR*)NULL, 0, NULL, 0);

				// Allocate statement handle  
				if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
					cout << "SQL DB Connect OK!!\n";

					retcode = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);

					char query[256] = {};
					char query_buf[30] = "EXEC update_user_data ";

					char kind_buf[10] = {};
					char gold_buf[10] = {};
					char level_buf[10] = {};
					char exp_buf[10] = {};
					char hp_buf[10] = {};
					char attack_buf[10] = {};
					char pos_x_buf[10] = {};
					char pos_y_buf[10] = {};

					wchar_t buf[256] = {};

					_itoa_s(clients[client].kind, kind_buf, 10);
					_itoa_s(clients[client].GOLD, gold_buf, 10);
					_itoa_s(clients[client].LEVEL, level_buf, 10);
					_itoa_s(clients[client].EXP, exp_buf, 10);
					_itoa_s(clients[client].HP, hp_buf, 10);
					_itoa_s(clients[client].ATTACK, attack_buf, 10);
					_itoa_s(clients[client].x, pos_x_buf, 10);
					_itoa_s(clients[client].y, pos_y_buf, 10);

					sprintf_s(query, "%s%s, %s, %s, %s, %s, %s, %s, %s, %s", query_buf, clients[client].login_id, 
						kind_buf, gold_buf, level_buf, exp_buf, hp_buf, attack_buf, pos_x_buf, pos_y_buf);

					cout << "결과 : " << query << endl;

					size_t wlen, len;
					len = strnlen_s(query, 256);
					mbstowcs_s(&wlen, buf, len + 1, query, len);

					// 여기를 봐라~!!~!!!!!!!!!!!!!!!!1
					// SQLExecDirect하면 SQL 명령어가 실행됨
					// EXEC 하고 내장함수 이름, 파라미터 주면 실행된다!!!!
					retcode = SQLExecDirect(hstmt, (SQLWCHAR *)buf, SQL_NTS);

					if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {

						// Bind columns 1, 2, and 3  
						//retcode = SQLBindCol(hstmt, 1, SQL_INTEGER, &uid, 100, &cb_uid);
						retcode = SQLBindCol(hstmt, 1, SQL_C_CHAR, uid, 20, &cb_uid);
						retcode = SQLBindCol(hstmt, 2, SQL_INTEGER, &ukind, 10, &cb_ukind);
						retcode = SQLBindCol(hstmt, 3, SQL_INTEGER, &ugold, 10, &cb_ugold);
						retcode = SQLBindCol(hstmt, 4, SQL_INTEGER, &ulevel, 10, &cb_ulevel);
						retcode = SQLBindCol(hstmt, 5, SQL_INTEGER, &uexp, 10, &cb_uexp);
						retcode = SQLBindCol(hstmt, 6, SQL_INTEGER, &uhp, 10, &cb_uhp);
						retcode = SQLBindCol(hstmt, 7, SQL_INTEGER, &uattack, 10, &cb_uattack);
						retcode = SQLBindCol(hstmt, 8, SQL_INTEGER, &uposx, 10, &cb_uposx);
						retcode = SQLBindCol(hstmt, 9, SQL_INTEGER, &uposy, 10, &cb_uposy);

						retcode = SQLFetch(hstmt);
						if (retcode == SQL_ERROR || retcode == SQL_SUCCESS_WITH_INFO)
							printf("error\n");
						if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
							wprintf(L"[%S] Lv. %d Exp. %d  X. %d Y. %d\n", uid, ulevel, uexp, uposx, uposy);
							return true;
						}
						else {
							// EOF일때..! EOD일수도 (End Of File / End Of Data)
							return false;
						}
					}
					else {
						db_err_display(hstmt, SQL_HANDLE_STMT, retcode);
					}

					// Process data  
					if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
						SQLCancel(hstmt);
						SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
					}

					SQLDisconnect(hdbc);
				}

				SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
			}
		}
		SQLFreeHandle(SQL_HANDLE_ENV, henv);
	}
}

bool is_sleeping(int id)
{
	return npcs[id].is_sleeping;
}
bool is_player_player_eyesight(int client, int other_client) {
	int x = clients[client].x - clients[other_client].x;
	int y = clients[client].y - clients[other_client].y;

	int distance = (x * x) + (y * y);

	if (distance < (VIEW_RADIUS * VIEW_RADIUS)) return true;
	else return false;
}
bool is_player_npc_eyesight(int client, int npc) {
	int x = clients[client].x - npcs[npc].x;
	int y = clients[client].y - npcs[npc].y;

	int distance = (x * x) + (y * y);

	if (distance < (VIEW_RADIUS * VIEW_RADIUS)) return true;
	else return false;
}
bool is_npc_eyesight(int client, int npc) {
	int x = clients[client].x - npcs[npc].x;
	int y = clients[client].y - npcs[npc].y;

	int distance = (x * x) + (y * y);

	if (distance < (NPC_VIEW_RADIUS * NPC_VIEW_RADIUS)) return true;
	else return false;
}
bool is_near_npc(int client, int npc) {
	int x = clients[client].x - npcs[npc].x;
	int y = clients[client].y - npcs[npc].y;

	int distance = (x * x) + (y * y);

	if (distance <= 1) return true;
	else return false;
}
bool is_level_up(int client) {
	int max_exp = (int)pow(2.0, clients[client].LEVEL) * 100;

	if (clients[client].EXP >= max_exp) {
		clients[client].EXP -= max_exp;
		clients[client].LEVEL++;
		clients[client].HP = cal_hp(client);
		clients[client].ATTACK = (int)pow(2.0, clients[client].LEVEL);
		add_db_evt(client, DB_EVT_UPDATE);
		return true;
	}
	else return false;
}

double kind_effect(int client, int npc) {
	if (clients[client].kind == FAIRY) {
		if (npcs[npc].kind == FAIRY) return 1.0;
		else if (npcs[npc].kind == ANGEL) return 2.0;
		else if (npcs[npc].kind == DEVIL) return 0.5;
	}
	else if (clients[client].kind == ANGEL) {
		if (npcs[npc].kind == FAIRY) return 0.5;
		else if (npcs[npc].kind == ANGEL) return 1.0;
		else if (npcs[npc].kind == DEVIL) return 2.0;

	}
	else if (clients[client].kind == DEVIL) {
		if (npcs[npc].kind == FAIRY) return 2.0;
		else if (npcs[npc].kind == ANGEL) return 0.5;
		else if (npcs[npc].kind == DEVIL) return 1.0;

	}
}
double npc_kind_effect(int client, int npc) {
	if (npcs[npc].kind == FAIRY) {
		if (clients[client].kind == FAIRY) return 1.0;
		else if (clients[client].kind == ANGEL) return 2.0;
		else if (clients[client].kind == DEVIL) return 0.5;
	}
	else if (npcs[npc].kind == ANGEL) {
		if (clients[client].kind == FAIRY) return 0.5;
		else if (clients[client].kind == ANGEL) return 1.0;
		else if (clients[client].kind == DEVIL) return 2.0;

	}
	else if (npcs[npc].kind == DEVIL) {
		if (clients[client].kind == FAIRY) return 2.0;
		else if (clients[client].kind == ANGEL) return 0.5;
		else if (clients[client].kind == DEVIL) return 1.0;

	}
}
double equip_effect(int client, int npc) {
	if (clients[client].item[CROWN] == 1
		&& clients[client].item[BREATH] == 1
		&& clients[client].item[SAINT_SWORD] == 1) {
		if (npcs[npc].kind == DRAGON) return 2.0;
	}

	if (clients[client].item[CROWN] == 1) {
		if (npcs[npc].kind == ANGEL) return 2.0;
	}
	if (clients[client].item[BREATH] == 1) {
		if (npcs[npc].kind == FAIRY) return 2.0;
	}
	if (clients[client].item[CROWN] == 1) {
		if (npcs[npc].kind == DEVIL) return 2.0;
	}
	return 1.0;
}
//double item_effect(int client) {
//
//}

int cal_hp(int client) {
	switch (clients[client].LEVEL) {
	case 1: return 150; break;
	case 2: return 450; break;
	case 3: return 900; break;
	case 4: return 1500; break;
	case 5: return 2250; break;
	case 6: return 3150; break;
	case 7: return 4200; break;
	case 8: return 5400; break;
	case 9: return 6750; break;
	case 10: return 8250; break;
	}
}

wchar_t* get_NPC_name(int npc_id) {
	wchar_t name_buf[MAX_STR_LEN];
	if (npc_id < 1500)	wcsncpy_s(name_buf, L"픽시", 256);
	else if (npc_id >= 1500 && npc_id < 3000) wcsncpy_s(name_buf, L"레비아탄", 256);
	else if (npc_id >= 3000 && npc_id < 4500) wcsncpy_s(name_buf, L"우리엘", 256);
	else if (npc_id >= 4500 && npc_id < 5250) wcsncpy_s(name_buf, L"퍽", 256);
	else if (npc_id >= 5250 && npc_id < 6000) wcsncpy_s(name_buf, L"베히모스", 256);
	else if (npc_id >= 6000 && npc_id < 6750) wcsncpy_s(name_buf, L"가브리엘", 256);
	else if (npc_id >= 6750 && npc_id < 7050) wcsncpy_s(name_buf, L"듀라한", 256);
	else if (npc_id >= 7050 && npc_id < 7350) wcsncpy_s(name_buf, L"마몬", 256);
	else if (npc_id >= 7350 && npc_id < 7650) wcsncpy_s(name_buf, L"메타트론", 256);
	else if (npc_id == 7650)  wcsncpy_s(name_buf, L"투명드래곤", 256);
	else if (npc_id == 7651)  wcsncpy_s(name_buf, L"티타니아", 256);
	else if (npc_id == 7652)  wcsncpy_s(name_buf, L"벨제뷔트", 256);
	else if (npc_id == 7653)  wcsncpy_s(name_buf, L"미카엘", 256);
	return name_buf;
}
void wakeup_NPC(int id)
{
	if (true == is_sleeping(id)) {
		npcs[id].is_sleeping = false;
		EVENT_ST ev;
		ev.obj_id = id;
		ev.type = EV_MOVE;
		ev.start_time = high_resolution_clock::now() + 1s;
		timer_lock.lock();
		timer_queue.push(ev);
		timer_lock.unlock();
	}
}
void random_move_NPC(int npc_id)
{
	int x = npcs[npc_id].x;
	int y = npcs[npc_id].y;

	unordered_set <int> old_vl;
	for (int i = 0; i < MAX_USER; ++i) {
		if (false == clients[i].in_use) continue;
		if (false == is_player_npc_eyesight(i, npc_id)) continue;
		old_vl.insert(i);
	}

	switch (rand() % 4) {
	case 0: if (x > 0) x--; break;
	case 1: if (x < (WORLD_WIDTH - 1)) x++; break;
	case 2: if (y > 0) y--; break;
	case 3: if (y < (WORLD_HEIGHT - 1)) y++; break;
	}

	npcs[npc_id].x = x;
	npcs[npc_id].y = y;

	unordered_set <int> new_vl;
	for (int i = 0; i < MAX_USER; ++i) {
		if (false == clients[i].in_use) continue;
		if (false == is_player_npc_eyesight(i, npc_id)) continue;
		new_vl.insert(i);
	}

	// 새로 만난 플레이어, 계속 보는 플레이어 처리
	for (auto pl : new_vl) {
		clients[pl].v_lock.lock();
		if (0 == clients[pl].npc_view_list.count(npc_id)) {
			clients[pl].npc_view_list.insert(npc_id);
			clients[pl].v_lock.unlock();

			send_put_npc_packet(pl, npc_id);
		}
		else {
			clients[pl].v_lock.unlock();
			send_npc_pos_packet(pl, npc_id);
		}
	}

	// 헤어진 플레이어 처리
	for (auto pl : old_vl) {
		if (0 == new_vl.count(pl)) {
			clients[pl].v_lock.lock();
			if (0 < clients[pl].npc_view_list.count(npc_id)) {
				clients[pl].npc_view_list.erase(npc_id);
				clients[pl].v_lock.unlock();

				send_remove_npc_packet(pl, npc_id);
			}
			else clients[pl].v_lock.unlock();

		}
	}
}

int API_load_NPC_info(lua_State *L)
{
	int npc_id = (int)lua_tonumber(L, -4);
	int init_HP = (int)lua_tonumber(L, -3);
	int init_x = (int)lua_tonumber(L, -2);
	int init_y = (int)lua_tonumber(L, -1);

	lua_pop(L, 5);

	npcs[npc_id].x = init_x;
	npcs[npc_id].y = init_y;
	npcs[npc_id].HP = init_HP;

	wcout << L"HP : " << npcs[npc_id].HP << L"LEVEL : " << npcs[npc_id].LEVEL 
		<< L" " << npcs[npc_id].x << L", " << npcs[npc_id].y;

	return 0;
}
int API_get_player_x(lua_State *L)
{
	int obj_id = (int)lua_tonumber(L, -1);
	lua_pop(L, 2);
	int x = clients[obj_id].x;
	lua_pushnumber(L, x);
	return 1;
}
int API_get_player_y(lua_State *L)
{
	int obj_id = (int)lua_tonumber(L, -1);
	lua_pop(L, 2);
	int y = clients[obj_id].y;
	lua_pushnumber(L, y);
	return 1;
}
int API_get_npc_x(lua_State *L)
{
	int obj_id = (int)lua_tonumber(L, -1);
	lua_pop(L, 2);
	int x = npcs[obj_id].x;
	lua_pushnumber(L, x);
	return 1;
}
int API_get_npc_y(lua_State *L)
{
	int obj_id = (int)lua_tonumber(L, -1);
	lua_pop(L, 2);
	int y = npcs[obj_id].y;
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

	send_npc_chat_packet(client_id, from_id, wmess);
	return 0;
}


void process_packet(int client, char *packet)
{
	cs_packet_login *tmp = reinterpret_cast<cs_packet_login *>(packet);

	switch (tmp->type) {
	case CS_LOGIN:
	{
		cs_packet_login *p = reinterpret_cast<cs_packet_login *>(packet);

		clients[client].login_kind = p->player_kind;
		wcout << "packet_login_kind " << p->player_kind << endl;
		wcout << "login_kind " << clients[client].login_kind << endl;
		strcpy_s(clients[client].login_id, p->player_id);
		add_db_evt(client, DB_EVT_SEARCH);

	}
		break;

	case CS_MOVE: 
	{
		cs_packet_move *p = reinterpret_cast<cs_packet_move *>(packet);

		int x = clients[client].x;
		int y = clients[client].y;

		clients[client].v_lock.lock();
		auto old_vl = clients[client].view_list;
		auto old_npc_vl = clients[client].npc_view_list;
		clients[client].v_lock.unlock();

		switch (p->direction)
		{
		case 0 : if (y > 0) y--; break;
		case 1 : if (y < (WORLD_HEIGHT - 1)) y++; break;
		case 2 : if (x > 0) x--; break;
		case 3 : if (x < (WORLD_WIDTH - 1)) x++; break;
		default:
			wcout << L"정의되지 않은 패킷 도착 오류!!\n";
		//	while (true);
		}

		clients[client].x = x;
		clients[client].y = y;

		unordered_set <int> new_vl;
		for (int pl = 0; pl < MAX_USER; ++pl) {
			if (pl == client) continue;
			if (false == clients[pl].in_use) continue;
			if (false == is_player_player_eyesight(pl, client)) continue;
			new_vl.insert(pl);
		}

		unordered_set <int> new_npc_vl;
		for (int npc_id = 0; npc_id < NUM_NPC; ++npc_id) {
			if (false == is_player_npc_eyesight(client, npc_id)) continue;
			if (true == npcs[npc_id].is_die) continue;
			new_npc_vl.insert(npc_id);
		}

		send_player_pos_packet(client, client);

		// 1. old_vl에도 있고 new_vl에도 있는 객체
		for (auto pl : old_vl) {
			if (0 == new_vl.count(pl)) continue;
			clients[pl].v_lock.lock();
			if (0 < clients[pl].view_list.count(client)) {
				clients[pl].v_lock.unlock();
				send_player_pos_packet(pl, client);
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

			clients[pl].v_lock.lock();
			if (0 == clients[pl].view_list.count(client)) {
				clients[pl].view_list.insert(client);
				clients[pl].v_lock.unlock();

				send_put_player_packet(pl, client);
			}
			else {
				clients[pl].v_lock.unlock();
				send_player_pos_packet(pl, client);
			}
		}
		// 3. old_vl에 있고 new_vl에는 없는 플레이어
		for (auto pl : old_vl) {
			if (0 < new_vl.count(pl)) continue;
			clients[client].v_lock.lock();
			clients[client].view_list.erase(pl);
			clients[client].v_lock.unlock();

			send_remove_player_packet(client, pl);

			clients[pl].v_lock.lock();
			if (0 < clients[pl].view_list.count(client)) {
				clients[pl].view_list.erase(client);
				clients[pl].v_lock.unlock();

				send_remove_player_packet(pl, client);
			}
			else 	clients[pl].v_lock.unlock();

		}

		// npc view list
		// 1. old_vl에 없고 new_vl에만 있는 플레이어
		for (auto npc : new_npc_vl) {
			if (0 < old_npc_vl.count(npc)) continue;
			clients[client].v_lock.lock();
			clients[client].npc_view_list.insert(npc);
			clients[client].v_lock.unlock();

			send_put_npc_packet(client, npc);

			wakeup_NPC(npc);
		}
		// 2. old_vl에 있고 new_vl에는 없는 플레이어
		for (auto npc : old_npc_vl) {
			if (0 < new_npc_vl.count(npc)) continue;

			clients[client].v_lock.lock();
			clients[client].npc_view_list.erase(npc);
			clients[client].v_lock.unlock();

			send_remove_npc_packet(client, npc);
		}

		for (auto monster : new_npc_vl)
		{
			OVER_EX *ex_over = new OVER_EX;
			ex_over->event_t = EV_PLAYER_MOVE_DETECT;
			ex_over->target_player = client;
			PostQueuedCompletionStatus(g_iocp, 1, monster, &ex_over->over);
		}
	}
		break;

	case CS_ATTACK :
	{
		cs_packet_attack *p = reinterpret_cast<cs_packet_attack *>(packet);
//		wcout << L"공격 요청 ID  :  " << clients[client].login_id;

		add_timer(client, EV_ATTACK, high_resolution_clock::now() + 1s);
	}
		break;

	case CS_CHAT :
	{	
		cs_packet_chat *p = reinterpret_cast<cs_packet_chat *>(packet);
	}
		break;

	case CS_LOGOUT :
	{
		cs_packet_logout *p = reinterpret_cast<cs_packet_logout *>(packet);
		if (clients[client].is_login) add_db_evt(client, DB_EVT_SAVE);
	}
		break;

	case CS_CCU_TEST:
	{
		int new_id = client;

		clients[new_id].x = rand() % WORLD_WIDTH;
		clients[new_id].y = rand() % WORLD_HEIGHT;
		clients[new_id].is_dummy = true;
		clients[new_id].is_login = true;
		clients[new_id].in_use = true;
		clients[new_id].HP = 9999;

		for (int i = 0; i < MAX_USER; ++i) {
			if (false == clients[i].in_use) continue;
			if (false == is_player_player_eyesight(new_id, i)) continue;
			if (i == new_id) continue;
			clients[i].v_lock.lock();
			clients[i].view_list.insert(new_id);
			clients[i].v_lock.unlock();

			send_put_player_packet(i, new_id);
		}
	}
		break;

	case CS_HOTSPOT_TEST:
	{
		int new_id = client;

		clients[new_id].x = clients[new_id].y = 150;
		clients[new_id].is_dummy = true;
		clients[new_id].is_login = true;
		clients[new_id].in_use = true;
		clients[new_id].HP = 9999;

		for (int i = 0; i < MAX_USER; ++i) {
			if (false == clients[i].in_use) continue;
			if (false == is_player_player_eyesight(new_id, i)) continue;
			if (i == new_id) continue;
			clients[i].v_lock.lock();
			clients[i].view_list.insert(new_id);
			clients[i].v_lock.unlock();

			send_put_player_packet(i, new_id);
		}

		for (int npc = 0; npc < NUM_NPC; ++npc) {
			if (false == is_player_npc_eyesight(new_id, npc)) continue;
			wakeup_NPC(npc);
			clients[new_id].v_lock.lock();
			clients[new_id].npc_view_list.insert(npc);
			clients[new_id].v_lock.unlock();

			send_put_npc_packet(new_id, npc);
		}

	}
	break;

	//case CS_TEST_MOVE:
	//{

	//}
	//break;
	default :
		wcout << L"정의되지 않은 패킷입니다! \n";
	}
}
void process_event(EVENT_ST &ev)
{
	switch (ev.type) {
	case EV_MOVE: {
		if (ev.obj_id > 7650) return;
		if (npcs[ev.obj_id].is_die) return;

		if (npcs[ev.obj_id].type == WAR) {
			if (npcs[ev.obj_id].target_user_id != -1
				&& is_near_npc(npcs[ev.obj_id].target_user_id, ev.obj_id)) {
				add_timer(ev.obj_id, EV_NPC_ATTACK,
					high_resolution_clock::now() + 1s);
				add_timer(ev.obj_id, EV_MOVE,
					high_resolution_clock::now() + 1s);
				return;

			}
		}


		bool player_is_near = false;

		for (int i = 0; i < MAX_USER; ++i) {
			if (false == clients[i].in_use) continue;
			if (false == is_npc_eyesight(i, ev.obj_id)) continue;
			player_is_near = true;
			break;
		}

		if (player_is_near) {
			if (npcs[ev.obj_id].move_type == YES_MOVE) {
				random_move_NPC(ev.obj_id);
				add_timer(ev.obj_id, EV_MOVE,
					high_resolution_clock::now() + 1s);

				return;
			}
			
			if (npcs[ev.obj_id].type == WAR) {
				if (npcs[ev.obj_id].target_user_id != -1) {
					random_move_NPC(ev.obj_id);
					add_timer(ev.obj_id, EV_MOVE,
						high_resolution_clock::now() + 1s);

					return;
				}
			}

		}
		else {
			npcs[ev.obj_id].is_sleeping = true;
		}
	}
				  break;

	case EV_ATTACK: {
		int client = ev.obj_id;

		clients[client].v_lock.lock();
		auto npc_vl = clients[client].npc_view_list;
		clients[client].v_lock.unlock();

		double damage = clients[client].ATTACK;

		for (auto npc_id : npc_vl) {
			if (is_near_npc(client, npc_id)) {
				damage = damage * kind_effect(client, npc_id) * equip_effect(client, npc_id);

				//wcout << L"damage : " << damage << endl;

				// 안죽었을 경우 피 깎고, 타겟 플레이어 없으면 타겟으로 지정
				if (npcs[npc_id].HP - (int)damage > 0) 
				{
					//wcout << L"BEFORE : " << npcs[npc_id].HP << endl;

					npcs[npc_id].HP -= (int)damage;

					//wcout << L"AFTER : " << npcs[npc_id].HP << endl;

//					send_npc_stat_change_packet(client, npc_id);
					wchar_t buf[MAX_STR_LEN] = {};
					wchar_t buf_id[20];
					size_t wlen, len;
					len = strnlen_s(clients[client].login_id, 20);
					mbstowcs_s(&wlen, buf_id, len + 1, clients[client].login_id, len);

					wchar_t npc_name[MAX_STR_LEN];
					wcsncpy_s(npc_name, get_NPC_name(npc_id), 256);

					wsprintf(buf, TEXT("%s%s%s %d%s"), buf_id, TEXT(" → "), npc_name, (int)damage, TEXT(" 데미지"));

					for (int i = 0; i < MAX_USER; ++i) {
						if (!clients[i].in_use) continue;
						send_system_chat_packet(i, client, buf);
					}

					if (npcs[npc_id].target_user_id == -1)
					{
						npcs[npc_id].target_user_id = client;
						if (npcs[npc_id].move_type == NO_MOVE 
							|| npcs[npc_id].type == PEACE)
						{
							add_timer(ev.obj_id, EV_NPC_ATTACK,
								high_resolution_clock::now() + 1s);

							return;
						}
					}
				}
				// 죽었을 경우, 죽고, 플레이어 뷰리스트에서 지우고, 지우는 패킷 보내고, 부활 이벤트 추가
				// 경험치, 골드얻고 만약 레벨업 하면 HP 증가, 경험치 제대로 계산하기..
				else {
					
					npcs[npc_id].HP = 0;
					npcs[npc_id].is_die = true;
					npcs[npc_id].is_sleeping = true;
					npcs[npc_id].target_user_id = -1;
					npcs[npc_id].x = npcs[npc_id].y = 500;

					wchar_t buf[MAX_STR_LEN] = {};
					wchar_t buf_id[20];
					size_t wlen, len;
					len = strnlen_s(clients[client].login_id, 20);
					mbstowcs_s(&wlen, buf_id, len + 1, clients[client].login_id, len);

					wchar_t npc_name[20];
					wcsncpy_s(npc_name, get_NPC_name(npc_id), 256);

					wsprintf(buf, TEXT("%s%s%s%s"), buf_id, TEXT(" → "), npc_name, TEXT(" 처치!"));

					for (int i = 0; i < MAX_USER; ++i) {
						if (!clients[i].in_use) continue;
						send_system_chat_packet(i, client, buf);
					}

					clients[client].GOLD += npcs[npc_id].GOLD;
					clients[client].EXP += npcs[npc_id].EXP;

					if (is_level_up(client)) {
						wchar_t buf[MAX_STR_LEN] = {};
						wchar_t buf_id[20];
						size_t wlen, len;
						len = strnlen_s(clients[client].login_id, 20);
						mbstowcs_s(&wlen, buf_id, len + 1, clients[client].login_id, len);

						wsprintf(buf, TEXT("%s%s"), buf_id, TEXT("님이 레벨업!"));


						for (int i = 0; i < MAX_USER; ++i) {
							if (!clients[i].in_use) continue;
							send_system_chat_packet(i, client, buf);
						}
					}
					send_player_stat_change_packet(client, client);
					send_npc_pos_packet(client, npc_id);

					for (int i = 0; i < MAX_USER; ++i) {
						if (!clients[i].in_use) continue;

						clients[i].v_lock.lock();
						if (clients[i].npc_view_list.count(npc_id) > 0) {
							clients[i].npc_view_list.erase(npc_id);
							clients[i].v_lock.unlock();
						}
						else clients[i].v_lock.unlock();
					}


					send_remove_npc_packet(client, npc_id);

					add_timer(npc_id, EV_NPC_RESURRECTION, high_resolution_clock::now() + 30s);
				}

			}
		}

	}
					break;

	case EV_NPC_ATTACK: {
		int npc_id = ev.obj_id;
		int target = npcs[npc_id].target_user_id;
		
		if (clients[target].is_dummy) return;

		if (npcs[npc_id].is_die) return;
		if (npcs[npc_id].is_sleeping) return;

		double damage = npcs[npc_id].ATTACK;

		if (is_near_npc(target, npc_id)) {
			damage = damage * npc_kind_effect(target, npc_id);
//			wcout << L"npc가 입힌 데미지 : " << damage;

			if (clients[target].HP - (int)damage > 0)
			{
				clients[target].HP -= (int)damage;

				send_player_stat_change_packet(target, target);

				wchar_t buf[MAX_STR_LEN] = {};
				wchar_t buf_id[20];
				size_t wlen, len;
				len = strnlen_s(clients[target].login_id, 20);
				mbstowcs_s(&wlen, buf_id, len + 1, clients[target].login_id, len);

				wchar_t npc_name[MAX_STR_LEN];
				wcsncpy_s(npc_name, get_NPC_name(npc_id), 256);

				wsprintf(buf, TEXT("%s%s%s %d%s"), npc_name, TEXT(" → "), buf_id, (int)damage, TEXT(" 데미지"));

				for (int i = 0; i < MAX_USER; ++i) {
					if (!clients[i].in_use) continue;
					send_system_chat_packet(i, target, buf);
				}
			}
			// 죽었을 경우, 죽고, 플레이어 뷰리스트에서 지우고, 지우는 패킷 보내고, 부활 이벤트 추가
			else {
				clients[target].HP = 0;
				clients[target].EXP = clients[target].EXP / 2;

				send_player_stat_change_packet(target, target);

				wchar_t buf[MAX_STR_LEN] = {};
				wchar_t buf_id[20];
				size_t wlen, len;
				len = strnlen_s(clients[target].login_id, 20);
				mbstowcs_s(&wlen, buf_id, len + 1, clients[target].login_id, len);

				wchar_t npc_name[20];
				wcsncpy_s(npc_name, get_NPC_name(npc_id), 256);

				wsprintf(buf, TEXT("%s%s%s%s"), npc_name, TEXT(" → "), buf_id, TEXT(" 처치!"));

				for (int i = 0; i < MAX_USER; ++i) {
					if (!clients[i].in_use) continue;
					send_system_chat_packet(i, target, buf);
				}

				clients[target].v_lock.lock();
				auto tmp_vl = clients[target].view_list;
				clients[target].v_lock.unlock();

				for (auto other_player : tmp_vl) {
					clients[other_player].v_lock.lock();
					if (clients[other_player].view_list.count(target) > 0) {
						clients[other_player].view_list.erase(target);
						clients[other_player].v_lock.unlock();
						send_remove_player_packet(other_player, target);
					}
					else clients[other_player].v_lock.unlock();

				}

				clients[target].v_lock.lock();
				clients[target].view_list.clear();
				clients[target].npc_view_list.clear();
				clients[target].v_lock.unlock();

				send_remove_player_packet(target, target);

				add_timer(target, EV_RESURRECTION, high_resolution_clock::now() + 1s);
			}
		}

	}
					break;

	case EV_RESURRECTION :
	{
		int new_id = ev.obj_id;

		clients[new_id].HP = cal_hp(new_id);

		if (clients[new_id].kind == FAIRY) {
			clients[new_id].x = 65;
			clients[new_id].y = 150;
		}
		else if (clients[new_id].kind == DEVIL) {
			clients[new_id].x = 150;
			clients[new_id].y = 235;
		}
		else if (clients[new_id].kind == ANGEL) {
			clients[new_id].x = 150;
			clients[new_id].y = 65;
		}
		add_db_evt(new_id, DB_EVT_UPDATE);

		send_put_player_packet(new_id, new_id); // 나한테 내 위치 보내기

		// 다른 플레이어의 뷰리스트에 나를 추가하고 전송
		for (int i = 0; i < MAX_USER; ++i) {
			if (false == clients[i].in_use) continue;
			if (false == is_player_player_eyesight(new_id, i)) continue;
			if (i == new_id) continue;

			clients[i].v_lock.lock();
			clients[i].view_list.insert(new_id);
			clients[i].v_lock.unlock();

			send_put_player_packet(i, new_id);
		}


		// 내 위치를 뷰리스트에 있는 다른 플레이어에게 전송
		for (int i = 0; i < MAX_USER; ++i) {
			if (false == clients[i].in_use) continue;
			if (i == new_id) continue;
			if (false == is_player_player_eyesight(i, new_id)) continue;

			clients[new_id].v_lock.lock();
			clients[new_id].view_list.insert(i);
			clients[new_id].v_lock.unlock();

			send_put_player_packet(new_id, i);
		}


		// 내 주변에 있는 NPC를 뷰리스트에 추가
		for (int i = 0; i < NUM_NPC; ++i) {
			if (false == is_player_npc_eyesight(new_id, i)) continue;

			clients[new_id].v_lock.lock();
			clients[new_id].npc_view_list.insert(i);
			clients[new_id].v_lock.unlock();

			send_put_npc_packet(new_id, i);

			wakeup_NPC(i);
		}

		wchar_t buf[MAX_STR_LEN] = {};
		wchar_t buf_id[20];
		size_t wlen, len;
		len = strnlen_s(clients[new_id].login_id, 20);
		mbstowcs_s(&wlen, buf_id, len + 1, clients[new_id].login_id, len);

		wsprintf(buf, TEXT("%s%s"), buf_id, TEXT("님 부활!!"));

		for (int i = 0; i < MAX_USER; ++i) {
			if (!clients[i].in_use) continue;
			send_system_chat_packet(i, new_id, buf);
		}

	}
	break;
	case EV_NPC_RESURRECTION:{
		npcs[ev.obj_id].target_user_id = -1;

		lua_State *L = npcs[ev.obj_id].L;
				
		npcs[ev.obj_id].l_lock.lock();
		lua_getglobal(L, "load_npc_info");
		lua_pcall(L, 0, 0, 0);
		npcs[ev.obj_id].l_lock.unlock();

		npcs[ev.obj_id].is_die = false;
		npcs[ev.obj_id].is_sleeping = true;
	} 
					break;

	default:
		cout << "Unknown Event Error!\n";
		while (true);
	}
}
void process_db_event(DB_EVENT_ST &ev) {
	int new_id = ev.client_id;
	switch (ev.type) {
	case DB_EVT_SEARCH:
		if (clients[new_id].is_dummy) return;
		if (search_user_id(new_id))
		{
			send_login_ok_packet(new_id);
			send_put_player_packet(new_id, new_id); // 나한테 내 위치 보내기

			// 다른 플레이어의 뷰리스트에 나를 추가하고 전송
			for (int i = 0; i < MAX_USER; ++i) {
				if (false == clients[i].in_use) continue;
				if (false == is_player_player_eyesight(new_id, i)) continue;
				if (i == new_id) continue;

				clients[i].v_lock.lock();
				clients[i].view_list.insert(new_id);
				clients[i].v_lock.unlock();

				send_put_player_packet(i, new_id);
			}


			// 내 위치를 뷰리스트에 있는 다른 플레이어에게 전송
			for (int i = 0; i < MAX_USER; ++i) {
				if (false == clients[i].in_use) continue;
				if (i == new_id) continue;
				if (false == is_player_player_eyesight(i, new_id)) continue;

				clients[new_id].v_lock.lock();
				clients[new_id].view_list.insert(i);
				clients[new_id].v_lock.unlock();

				send_put_player_packet(new_id, i);
			}


			// 내 주변에 있는 NPC를 뷰리스트에 추가
			for (int i = 0; i < NUM_NPC; ++i) {
				if (false == is_player_npc_eyesight(new_id, i)) continue;

				clients[new_id].v_lock.lock();
				clients[new_id].npc_view_list.insert(i);
				clients[new_id].v_lock.unlock();

				send_put_npc_packet(new_id, i);

				wakeup_NPC(i);
			}

			for (int i = 0; i < MAX_USER; ++i) {
				if (!clients[i].in_use) continue;
				wchar_t buf[MAX_STR_LEN] = {};
				wchar_t buf_id[20];
				size_t wlen, len;
				len = strnlen_s(clients[new_id].login_id, 20);
				mbstowcs_s(&wlen, buf_id, len + 1, clients[new_id].login_id, len);

				wsprintf(buf, TEXT("%s%s"), buf_id, TEXT("님이 로그인하셨습니다."));
				send_system_chat_packet(i, new_id, buf);
			}
		}
		else {
			wchar_t buf[MAX_STR_LEN] = {};
			wchar_t buf_id[20];
			size_t wlen, len;
			len = strnlen_s(clients[new_id].login_id, 20);
			mbstowcs_s(&wlen, buf_id, len + 1, clients[new_id].login_id, len);

			wsprintf(buf, TEXT("%s%s"), buf_id, TEXT("은(는) 중복된 ID입니다."));
			send_system_chat_packet(new_id, new_id, buf);

			send_login_fail_packet(new_id);
			disconnect_client(new_id);
		}

		break;

	case DB_EVT_SAVE:
		if (clients[new_id].is_dummy) return;

		if (save_user_data(new_id)) {
			wcout << L"클라이언트 정보 저장 성공!\n";
		}
		else {
			wcout << L"클라이언트 정보 저장 실패!!!\n";
		}
//		wcout << L"연결을 종료합니다! \n";
		clients[new_id].is_login = false;
		disconnect_client(new_id);

		break;

	case DB_EVT_UPDATE:
		if (clients[new_id].is_dummy) return;

		if (save_user_data(new_id)) {
			wcout << L"클라이언트 정보 저장 성공!\n";
		}
		else {
			wcout << L"클라이언트 정보 저장 실패!!!\n";
		}
		break;

	default:
		break;
	}
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
void send_login_ok_packet(int new_id)
{
	sc_packet_login_ok packet;
	packet.id = new_id;
	packet.size = sizeof(packet);
	packet.type = SC_LOGIN_OK;

	packet.kind = clients[new_id].kind;
	packet.x = clients[new_id].x;
	packet.y = clients[new_id].y;
	packet.LEVEL = clients[new_id].LEVEL;
	packet.HP = clients[new_id].HP;
	packet.ATTACK = clients[new_id].ATTACK;
	packet.EXP = clients[new_id].EXP;
	packet.GOLD = clients[new_id].GOLD;

	send_packet(new_id, &packet);
}
void send_login_fail_packet(int new_id) {
	sc_packet_login_fail packet;
	packet.size = sizeof(packet);
	packet.type = SC_LOGIN_FAIL;

	send_packet(new_id, &packet);

}
void send_put_player_packet(int client, int new_id)
{
	sc_packet_add_object packet;
	packet.size = sizeof(packet);
	packet.type = SC_ADD_OBJECT;
	packet.obj_class = PLAYER;

	packet.id = new_id;
	packet.kind = clients[new_id].kind;
	packet.x = clients[new_id].x;
	packet.y = clients[new_id].y;
	packet.HP = clients[new_id].HP;
	packet.LEVEL = clients[new_id].LEVEL;
	packet.EXP = clients[new_id].EXP;

	send_packet(client, &packet);
}
void send_put_npc_packet(int client, int new_npc_id)
{
	sc_packet_add_object packet;
	packet.size = sizeof(packet);
	packet.type = SC_ADD_OBJECT;
	packet.obj_class = NPC;

	packet.id = new_npc_id;
	packet.kind = npcs[new_npc_id].kind;
	packet.x = npcs[new_npc_id].x;
	packet.y = npcs[new_npc_id].y;
	packet.HP = npcs[new_npc_id].HP;
	packet.LEVEL = npcs[new_npc_id].LEVEL;
	packet.EXP = npcs[new_npc_id].EXP;

	send_packet(client, &packet);
}
void send_remove_player_packet(int client, int remove_id)
{
	sc_packet_remove_object packet;
	packet.size = sizeof(packet);
	packet.type = SC_REMOVE_OBJECT;
	packet.obj_class = PLAYER;
	packet.id = remove_id;

	send_packet(client, &packet);
}
void send_remove_npc_packet(int client, int remove_npc_id)
{
	sc_packet_remove_object packet;
	packet.size = sizeof(packet);
	packet.type = SC_REMOVE_OBJECT;
	packet.obj_class = NPC;
	packet.id = remove_npc_id;

	send_packet(client, &packet);
}
void send_player_pos_packet(int client, int pl)
{
	sc_packet_position packet;
	packet.size = sizeof(packet);
	packet.type = SC_POSITION;
	packet.obj_class = PLAYER;
	packet.id = pl;
	packet.x = clients[pl].x;
	packet.y = clients[pl].y;

	send_packet(client, &packet);
}
void send_npc_pos_packet(int client, int npc)
{
	sc_packet_position packet;
	packet.size = sizeof(packet);
	packet.type = SC_POSITION;
	packet.obj_class = NPC;
	packet.id = npc;
	packet.x = npcs[npc].x;
	packet.y = npcs[npc].y;

	send_packet(client, &packet);
}
void send_player_stat_change_packet(int client, int player)
{
	sc_packet_stat_change packet;
	packet.size = sizeof(packet);
	packet.type = SC_STAT_CHANGE;
	packet.obj_class = PLAYER;
	packet.id = player;
	packet.HP = clients[client].HP;
	packet.LEVEL = clients[client].LEVEL;
	packet.EXP = clients[client].EXP;
	packet.GOLD = clients[client].GOLD;

	send_packet(client, &packet);
}
void send_npc_stat_change_packet(int client, int npc)
{
	sc_packet_stat_change packet;
	packet.size = sizeof(packet);
	packet.type = SC_STAT_CHANGE;
	packet.obj_class = NPC;
	packet.id = npc;
	packet.HP = npcs[client].HP;
	packet.LEVEL = npcs[client].LEVEL;
	packet.EXP = npcs[client].EXP;

	send_packet(client, &packet);
}
void send_system_chat_packet(int client, int from_id, wchar_t *mess)
{
	sc_packet_chat packet;
	packet.id = from_id;
	packet.size = sizeof(packet);
	packet.type = SC_CHAT;
	packet.obj_class = PLAYER;
	wcscpy_s(packet.message, mess);
	send_packet(client, &packet);
}
void send_player_chat_packet(int client, int from_id, wchar_t *mess)
{
	sc_packet_chat packet;
	packet.id = from_id;
	packet.size = sizeof(packet);
	packet.type = SC_CHAT;
	packet.obj_class = PLAYER;
	wcscpy_s(packet.message, mess);
	send_packet(client, &packet);
}
void send_npc_chat_packet(int client, int from_id, wchar_t *mess)
{
	sc_packet_chat packet;
	packet.id = from_id;
	packet.size = sizeof(packet);
	packet.type = SC_CHAT;
	packet.obj_class = NPC;
	wcscpy_s(packet.message, mess);
	send_packet(client, &packet);
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
	thread timer_thread{ do_timer };
	thread db_thread{ do_transaction };

	timer_thread.join();
	accept_thread.join();
	db_thread.join();

	for (auto &th : worker_threads) th.join();
}