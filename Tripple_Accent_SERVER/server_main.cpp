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
	bool in_use;
	OVER_EX over_ex;
	SOCKET socket;
	char packet_buffer[MAX_BUFFER];
	int	prev_size;

	bool is_login;

	char  login_id[MAX_ID_LEN];
	char  kind;
	short x, y;
	unsigned short HP, LEVEL, ATTACK;
	int   EXP, GOLD;

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
	bool is_movable;
	char kind;
	char type;
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
	}
}
void Initialize_NPC()
{
	for (int npc_id = 0; npc_id < 10000; ++npc_id) {
		npcs[npc_id].kind = FAIRY;
		
		// 픽시
		if (npc_id < 3500) {
			npcs[npc_id].type = PEACE;
			npcs[npc_id].is_movable = false;
			npcs[npc_id].LEVEL = 5;
			npcs[npc_id].EXP = 25;
			npcs[npc_id].GOLD = 2;
			npcs[npc_id].HP = 10;
			npcs[npc_id].ATTACK = 10;
		}
		// 퍽
		else if (npc_id >= 3500 && npc_id < 7000) {
			npcs[npc_id].type = WAR;
			npcs[npc_id].is_movable = false;
			npcs[npc_id].LEVEL = 50;
			npcs[npc_id].EXP = 500;
			npcs[npc_id].GOLD = 30;
			npcs[npc_id].HP = 450;
			npcs[npc_id].ATTACK = 210;
		}
		// 듀라한
		else {
			npcs[npc_id].type = WAR;
			npcs[npc_id].is_movable = true;
			npcs[npc_id].LEVEL = 105;
			npcs[npc_id].EXP = 2100;
			npcs[npc_id].GOLD = 450;
			npcs[npc_id].HP = 1850;
			npcs[npc_id].ATTACK = 350;

		}

		npcs[npc_id].x = rand() % 200 + 50;
		npcs[npc_id].y = rand() % 100 + 175;
	}

	for (int npc_id = 10000; npc_id < 20000; ++npc_id) {
		npcs[npc_id].kind = DEVIL;

		// 레비아탄
		if (npc_id < 13500) {
			npcs[npc_id].type = WAR;
			npcs[npc_id].is_movable = true;
			npcs[npc_id].LEVEL = 3;
			npcs[npc_id].EXP = 60;
			npcs[npc_id].GOLD = 1;
			npcs[npc_id].HP = 20;
			npcs[npc_id].ATTACK = 25;
		}
		// 베히모스
		else if (npc_id >= 13500 && npc_id < 17000) {
			npcs[npc_id].type = PEACE;
			npcs[npc_id].is_movable = false;
			npcs[npc_id].LEVEL = 45;
			npcs[npc_id].EXP = 225;
			npcs[npc_id].GOLD = 70;
			npcs[npc_id].HP = 280;
			npcs[npc_id].ATTACK = 150;
		}
		// 마몬
		else {
			npcs[npc_id].type = PEACE;
			npcs[npc_id].is_movable = true;
			npcs[npc_id].LEVEL = 80;
			npcs[npc_id].EXP = 800;
			npcs[npc_id].GOLD = 180;
			npcs[npc_id].HP = 900;
			npcs[npc_id].ATTACK = 270;
		}

		npcs[npc_id].x = rand() % 100 + 175;
		npcs[npc_id].y = rand() % 200 + 50;
	}
	
	for (int npc_id = 20000; npc_id < 30000; ++npc_id) {
		npcs[npc_id].kind = DEVIL;

		// 우리엘
		if (npc_id < 23500) {
			npcs[npc_id].type = WAR;
			npcs[npc_id].is_movable = true;
			npcs[npc_id].LEVEL = 4;
			npcs[npc_id].EXP = 80;
			npcs[npc_id].GOLD = 3;
			npcs[npc_id].HP = 60;
			npcs[npc_id].ATTACK = 30;
		}
		// 가브리엘
		else if (npc_id >= 13500 && npc_id < 17000) {
			npcs[npc_id].type = PEACE;
			npcs[npc_id].is_movable = false;
			npcs[npc_id].LEVEL = 20;
			npcs[npc_id].EXP = 100;
			npcs[npc_id].GOLD = 10;
			npcs[npc_id].HP = 110;
			npcs[npc_id].ATTACK = 135;
		}
		// 메타트론
		else {
			npcs[npc_id].type = WAR;
			npcs[npc_id].is_movable = true;
			npcs[npc_id].LEVEL = 160;
			npcs[npc_id].EXP = 3200;
			npcs[npc_id].GOLD = 720;
			npcs[npc_id].HP = 3350;
			npcs[npc_id].ATTACK = 480;
		}

		npcs[npc_id].x = rand() % 100 + 75;
		npcs[npc_id].y = rand() % 200 + 50;
	}

	// 투명 드래곤
	npcs[NUM_NPC - 1].kind = DRAGON;
	npcs[NUM_NPC - 1].type = WAR;
	npcs[NUM_NPC - 1].is_movable = true;
	npcs[NUM_NPC - 1].LEVEL = 330;
	npcs[NUM_NPC - 1].EXP = 6600;
	npcs[NUM_NPC - 1].GOLD = 3200;
	npcs[NUM_NPC - 1].HP = 65535;
	npcs[NUM_NPC - 1].ATTACK = 250;
	npcs[NUM_NPC - 1].x = 150;
	npcs[NUM_NPC - 1].y = 150;
	
	for (int npc_id = 0; npc_id < NUM_NPC; ++npc_id) {
		npcs[npc_id].target_user_id = -1;

		npcs[npc_id].is_sleeping = true;
		npcs[npc_id].is_die = false;

		add_timer(npc_id, EV_MOVE, high_resolution_clock::now() + 1s);

		lua_State *L = npcs[npc_id].L;
		lua_getglobal(L, "set_npc_info");
		lua_pushnumber(L, npc_id);
		lua_pushnumber(L, npcs[npc_id].type);
		lua_pushnumber(L, npcs[npc_id].kind);
		lua_pcall(L, 3, 0, 0);

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
			DB_EVENT_ST dev;
			dev.client_id = key;
			dev.type = DB_EVT_SEARCH;

			process_db_event(dev);
			delete over_ex;
		}
		else if (DB_EVT_SAVE == over_ex->event_t) {
			DB_EVENT_ST dev;
			dev.client_id = key;
			dev.type = DB_EVT_SAVE;

			process_db_event(dev);
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
		else if (EV_PLAYER_MOVE_DETECT == over_ex->event_t) {
			lua_State *L = npcs[key].L;
			int player_id = over_ex->target_player;

			npcs[key].l_lock.lock();
			lua_getglobal(L, "event_player_move");
			lua_pushnumber(L, player_id);
			lua_pcall(L, 1, 0, 0);
			npcs[key].l_lock.unlock();


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
			over_ex->event_t = EV_MOVE;
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
					cout << "SQL DB Connect OK!!\n";

					retcode = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);


					char query[128] = "EXEC select_user_id ";
					strncat_s(query, clients[client].login_id, MAX_ID_LEN);
										cout << "strcat 결과 : " << query << endl;

					size_t wlen, len;
					len = strnlen_s(query, 128);
					mbstowcs_s(&wlen, buf, len + 1, query, len);

					wcout << buf << endl;

					retcode = SQLExecDirect(hstmt, (SQLWCHAR *)buf, SQL_NTS);

					if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {

						// Bind columns 1, 2, and 3  
						//retcode = SQLBindCol(hstmt, 1, SQL_INTEGER, &uid, 100, &cb_uid);
						retcode = SQLBindCol(hstmt, 1, SQL_C_CHAR, uid, 20, &cb_uid);
						retcode = SQLBindCol(hstmt, 2, SQL_INTEGER, &ukind, 10, &cb_ukind);
						retcode = SQLBindCol(hstmt, 3, SQL_INTEGER, &ugold , 10, &cb_ugold);
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
							wprintf(L"No Search ID!! \n");
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
					wcout << buf << endl;

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
bool is_level_up(int client) {
	int max_exp = (int)pow(2.0, clients[client].LEVEL) * 100;

	if (clients[client].EXP > max_exp) return true;
	else return false;
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

		wcout << L"접속 요청 ID : " << p->player_id << '\n';
		strcpy_s(clients[client].login_id, p->player_id);
		wcout << L"ID : " << clients[client].login_id << '\n';
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

		clients[client].is_login = true;

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

	case CS_HOTSPOT_TEST:
	{
		int new_id = client;

		clients[new_id].x = clients[new_id].y = 150;

		clients[client].is_login = true;

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

	default :
		wcout << L"정의되지 않은 패킷입니다! \n";
	}
}
void process_event(EVENT_ST &ev)
{
	switch (ev.type) {
	case EV_MOVE: {
		bool player_is_near = false;
		for (int i = 0; i < MAX_USER; ++i) {
			if (false == clients[i].in_use) continue;
			if (false == is_npc_eyesight(i, ev.obj_id)) continue;
			player_is_near = true;
			break;
		}
		if (player_is_near) {
			random_move_NPC(ev.obj_id);
			add_timer(ev.obj_id, EV_MOVE,
				high_resolution_clock::now() + 1s);
		}
		else {
			npcs[ev.obj_id].is_sleeping = true;
		}
		break;
	}
	default:
		cout << "Unknown Event Error!\n";
		while (true);
	}
}
void process_db_event(DB_EVENT_ST &ev) {
	int new_id = ev.client_id;
	switch (ev.type) {
	case DB_EVT_SEARCH:
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

		}
		else {
			send_login_fail_packet(new_id);
			disconnect_client(new_id);
		}

		break;

	case DB_EVT_SAVE:
		if (save_user_data(new_id)) {
			wcout << L"저장 성공!\n";
		}
		else {
			wcout << L"저장 실패!!!\n";
		}
		wcout << L"연결을 종료합니다! \n";
		clients[new_id].is_login = false;
		disconnect_client(new_id);

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