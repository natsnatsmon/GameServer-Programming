/*
## 소켓 서버 : 1 v n - overlapped callback
1. socket()            : 소켓생성
2. bind()            : 소켓설정
3. listen()            : 수신대기열생성
4. accept()            : 연결대기
5. read()&write()
WIN recv()&send    : 데이터 읽고쓰기
6. close()
WIN closesocket    : 소켓종료
*/

#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <iostream>
using namespace std;
#include <winsock2.h>
#include "protocol.h"
#include <thread>
#include <vector>
#include <mutex>
#include <unordered_set>
#include <queue>
#include <chrono>
using namespace chrono;
#include <locale.h>
#include <windows.h>  
#include <stdio.h>  
#define UNICODE  
#include <sqlext.h>  

extern "C" {
#include "include\lua.h"
#include "include\lauxlib.h"
#include "include\lualib.h"
}

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "lua53.lib")

#define MAX_BUFFER        1024
#define VIEW_RADIUS			8
#define NPC_RADIUS			9
#define MAX_ID				20

enum EVENT_TYPE {
	EVT_PLAYER_MOVE_DETECT,
	EVT_MOVE, EVT_HEAL, EVT_RECV, EVT_SEND,
	DB_EVT_SEARCH, DB_EVT_SAVE
};

// 오버랩드 구조체를 확장해서 쓰자
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
	// 클라이언트마다 하나씩 있어야댐.. 하나로 공유하면 덮어써버리는거다... 밑에 4개는 클라마다 꼭 있어야 하는 것.. 必수要소
	mutex view_lock;
	OVER_EX over_ex;
	SOCKET socket;
	char packetBuffer[MAX_BUFFER];
	int prev_size;
	int x, y;
	bool is_login_ok;
	char login_id[MAX_ID];
	unordered_set <int> viewlist;
	unordered_set <int> npc_viewlist;

	SOCKETINFO() {
		in_use = false;
		over_ex.dataBuffer.len = MAX_BUFFER;
		over_ex.dataBuffer.buf = over_ex.messageBuffer;
		over_ex.event_t = EVT_RECV;
	}
};
SOCKETINFO clients[MAX_USER];

// NPC 구현 첫번째
class NPCINFO {
public:
	int x, y;
	bool is_sleeping;
	lua_State *L;
	mutex lua_lock;

	NPCINFO() {
		is_sleeping = true;
		L = luaL_newstate();
		luaL_openlibs(L);

		luaL_loadfile(L, "monster_ai.lua");
		lua_pcall(L, 0, 0, 0);
	}
};
NPCINFO npcs[MAX_NPC];

struct EVENT_ST {
	int obj_id;
	EVENT_TYPE type;
	high_resolution_clock::time_point start_time;
	constexpr bool operator < (const EVENT_ST& _Left)const {
		return(start_time > _Left.start_time);
	}
};

struct DB_EVENT_ST {
	int client_id;
	EVENT_TYPE type;
};

mutex timer_lock;
mutex db_lock;

priority_queue<EVENT_ST> timer_queue;
queue<DB_EVENT_ST> db_queue;

HANDLE g_iocp;

void err_display(const char * msg, int err_no);
void db_err_display(SQLHANDLE hHandle, SQLSMALLINT hType, RETCODE RetCode);
void add_timer(int obj_id, EVENT_TYPE et, high_resolution_clock::time_point start_time);
void add_db_evt(int client_id, EVENT_TYPE et);

int API_get_x(lua_State *L);
int API_get_y(lua_State *L);
int API_send_message(lua_State *L);

void Init();

int do_accept();
void do_timer();
void do_transaction();
void do_recv(int id);

bool search_user_id(int client);
bool save_user_data(int client);

void send_packet(int client, void *packet);
void send_chat_packet(int clients, int new_id, wchar_t *msg);

void send_pos_packet(int client, int pl);
void send_npc_pos_packet(int client, int npc);

void send_login_ok_packet(int new_id);

void send_remove_player_packet(int clinet, int id);
void send_remove_npc_packet(int client, int npc);

void send_put_player_packet(int clients, int new_id);
void send_put_npc_packet(int clients, int npc);

void process_packet(int client, char *packet);
void process_event(EVENT_ST &ev);
void process_db_event(DB_EVENT_ST &ev);

void disconnect_client(int id);

void worker_thread();

void move_npc(int npc_id);
void wakeup_NPC(int npc_id);

bool is_eyesight(int client_id, int other_client_id);
bool is_player_npc_eyesight(int client, int npc);
bool is_npc_eyesight(int client_id, int npc_id);
bool is_sleeping(int npc_id);

//bool Is_Near_Object(int a, int b);

int main() {

	vector <thread> worker_threads;

	wcout.imbue(locale("korean"));

	Init();

	g_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);
	for (int i = 0; i < 4; ++i) {
		worker_threads.emplace_back(thread{ worker_thread });
	}

	thread accept_thread{ do_accept };
	thread timer_thread{ do_timer };
	thread db_thread{ do_transaction };
	accept_thread.join();
	timer_thread.join();
	db_thread.join();

	for (auto &th : worker_threads) {
		th.join();
	}
}


void err_display(const char * msg, int err_no)
{
	WCHAR *lpMsgBuf;
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, err_no, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPWSTR)&lpMsgBuf, 0, NULL);

	cout << msg;
	wcout << L"에러  [" << err_no << L"] " << lpMsgBuf << endl;
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

void add_timer(int obj_id, EVENT_TYPE et, high_resolution_clock::time_point start_time) {
	timer_lock.lock();
	timer_queue.emplace(EVENT_ST{ obj_id,et,start_time });
	timer_lock.unlock();
}
void add_db_evt(int client_id, EVENT_TYPE et) {
	db_lock.lock();
	db_queue.emplace(DB_EVENT_ST{ client_id, et });
	db_lock.unlock();
}

int API_get_x(lua_State *L) {
	int obj_id = (int)lua_tonumber(L, -1);
	lua_pop(L, 2);
	int x = npcs[obj_id].x;
	lua_pushnumber(L, x);
	return 1;
}
int API_get_y(lua_State *L) {
	int obj_id = (int)lua_tonumber(L, -1);
	lua_pop(L, 2);
	int y = npcs[obj_id].y;
	lua_pushnumber(L, y);
	return 1;
}
int API_send_message(lua_State *L) {
	int client_id = (int)lua_tonumber(L, -3);
	int from_id = (int)lua_tonumber(L, -2);
	char *mess = (char *)lua_tostring(L, -1); // 루아는 유니코드를 지원하지 않는다..
	wchar_t wmess[MAX_STR_LEN];

	lua_pop(L, 4);

	size_t wlen, len;
	len = strnlen_s(mess, MAX_STR_LEN);
	mbstowcs_s(&wlen, wmess, len, mess, _TRUNCATE);

	send_chat_packet(client_id, from_id, wmess);
	return 1;
}

void Init() {
	for (int pl_id = 0; pl_id < MAX_USER; ++pl_id) {
		clients[pl_id].in_use = false;
		clients[pl_id].is_login_ok = false;
	}

	for (int npc_id = 0; npc_id < MAX_NPC; ++npc_id) {
		npcs[npc_id].x = rand() % WORLD_WIDTH;
		npcs[npc_id].y = rand() % WORLD_HEIGHT;
		npcs[npc_id].is_sleeping = true;
		add_timer(npc_id, EVT_MOVE, high_resolution_clock::now() + 1s);

		lua_State *L = npcs[npc_id].L;
		lua_getglobal(L, "set_uid");
		lua_pushnumber(L, npc_id);
		lua_pcall(L, 1, 0, 0);
		lua_register(L, "API_get_x", API_get_x);
		lua_register(L, "API_get_y", API_get_y);
		lua_register(L, "API_send_message", API_send_message);
	}

	wcout << L"** Initialize 완료!! ** \n \n \n";
}


int do_accept()
{
	// Winsock Start - winsock.dll 로드
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
	//	memset(&serverAddr, 0, sizeof(SOCKADDR_IN));
	serverAddr.sin_family = PF_INET;
	serverAddr.sin_port = htons(SERVER_PORT);
	serverAddr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);

	// 2. 소켓설정
	if (::bind(listenSocket, (struct sockaddr*)&serverAddr, sizeof(SOCKADDR_IN)) == SOCKET_ERROR)
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
	//	memset(&clientAddr, 0, addrLen);
	SOCKET clientSocket;
	DWORD flags;

	while (1)
	{
		// listenSocket에 비동기 설정을 해줘야 clientSocket도 비동기가 된다!
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

			if (-1 == new_id) {
				cout << "MAX UVER overlow \n";
				continue;
			}
		}

		// recv 준비
		clients[new_id].socket = clientSocket;
		clients[new_id].prev_size = 0;
		clients[new_id].x = clients[new_id].y = 50;
		clients[new_id].view_lock.lock();
		clients[new_id].viewlist.clear(); // 뷰리스트 초기화
		clients[new_id].view_lock.unlock();
		ZeroMemory(&clients[new_id].over_ex.over,
			sizeof(clients[new_id].over_ex.over));
		flags = 0;

		CreateIoCompletionPort(reinterpret_cast<HANDLE>(clientSocket), g_iocp, new_id, 0);

		clients[new_id].in_use = true; // IOCP에 등록 한 다음 true로 바꿔줘야 데이터를 받을 수 있당

		do_recv(new_id);
	}

	// 6-2. 리슨 소켓종료
	closesocket(listenSocket);

	// Winsock End
	WSACleanup();

	return 0;
}

void do_recv(int id) {
	DWORD flags = 0;

	// 소켓, 버퍼 주소, 크기, 넘버 오브 리시브 어쩌고, 플래그, 이 클라 전용으로 만들어놓은 오버랩드 구조체, 콜백함수 포인터..
	if (WSARecv(clients[id].socket, &clients[id].over_ex.dataBuffer, 1,
		NULL, &flags, &(clients[id].over_ex.over), 0))
	{
		if (WSAGetLastError() != WSA_IO_PENDING)
		{
			cout << "Error - IO pending Failure\n";
			while (true);
		}
	}
	else {
		cout << "Non Overlapped Recv return.\n";
	}

}

bool search_user_id(int client) {
	SQLHENV henv;
	SQLHDBC hdbc;
	SQLHSTMT hstmt = 0;
	SQLRETURN retcode;

	SQLWCHAR uid[20];
	SQLINTEGER uposx, uposy;
	SQLLEN cb_uid = 0, cb_uposx = 0, cb_uposy = 0;
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
				retcode = SQLConnect(hdbc, (SQLWCHAR*)L"2019_WT_2016184017", SQL_NTS, (SQLWCHAR*)NULL, 0, NULL, 0);

				// Allocate statement handle  
				if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
					cout << "SQL DB Connect OK!!\n";

					retcode = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);


					char query[128] = "EXEC select_user_id ";
					strcat(query, clients[client].login_id);
					//					cout << "strcat 결과 : " << query << endl;

					mbstowcs(buf, query, strlen(query));
					wcout << buf << endl;
					// 여기를 봐라~!!~!!!!!!!!!!!!!!!!1
					// SQLExecDirect하면 SQL 명령어가 실행됨
					// EXEC 하고 내장함수 이름, 파라미터 주면 실행된다!!!!
					retcode = SQLExecDirect(hstmt, (SQLWCHAR *)buf, SQL_NTS);

					if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {

						// Bind columns 1, 2, and 3  
						//retcode = SQLBindCol(hstmt, 1, SQL_INTEGER, &uid, 100, &cb_uid);
						retcode = SQLBindCol(hstmt, 1, SQL_C_CHAR, uid, 20, &cb_uid);
						retcode = SQLBindCol(hstmt, 2, SQL_INTEGER, &uposx, 10, &cb_uposx);
						retcode = SQLBindCol(hstmt, 3, SQL_INTEGER, &uposy, 10, &cb_uposy);

						retcode = SQLFetch(hstmt);
						if (retcode == SQL_ERROR || retcode == SQL_SUCCESS_WITH_INFO)
							printf("error\n");
						if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
							wprintf(L"%S : %d %d\n", uid, uposx, uposy);
							clients[client].x = uposx;
							clients[client].y = uposy;
							clients[client].is_login_ok = true;
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
bool save_user_data(int client) {
	SQLHENV henv;
	SQLHDBC hdbc;
	SQLHSTMT hstmt = 0;
	SQLRETURN retcode;

	SQLWCHAR uid[20];
	SQLINTEGER uposx, uposy;
	SQLLEN cb_uid = 0, cb_uposx = 0, cb_uposy = 0;

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
				retcode = SQLConnect(hdbc, (SQLWCHAR*)L"2019_WT_2016184017", SQL_NTS, (SQLWCHAR*)NULL, 0, NULL, 0);

				// Allocate statement handle  
				if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
					cout << "SQL DB Connect OK!!\n";

					retcode = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);

					char query[128] = {};
					char query_buf[30] = "EXEC update_user_data ";
					char pos_x_buf[10] = {};
					char pos_y_buf[10] = {};
					wchar_t buf[128] = {};

					_itoa(clients[client].x, pos_x_buf, 10);
					_itoa(clients[client].y, pos_y_buf, 10);

					sprintf(query, "%s%s, %s, %s", query_buf, clients[client].login_id, pos_x_buf, pos_y_buf);

					cout << "결과 : " << query << endl;

					mbstowcs(buf, query, strlen(query));
					wcout << buf << endl;

					// 여기를 봐라~!!~!!!!!!!!!!!!!!!!1
					// SQLExecDirect하면 SQL 명령어가 실행됨
					// EXEC 하고 내장함수 이름, 파라미터 주면 실행된다!!!!
					retcode = SQLExecDirect(hstmt, (SQLWCHAR *)buf, SQL_NTS);

					if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {

						// Bind columns 1, 2, and 3  
						//retcode = SQLBindCol(hstmt, 1, SQL_INTEGER, &uid, 100, &cb_uid);
						retcode = SQLBindCol(hstmt, 1, SQL_C_CHAR, uid, 20, &cb_uid);
						retcode = SQLBindCol(hstmt, 2, SQL_INTEGER, &uposx, 10, &cb_uposx);
						retcode = SQLBindCol(hstmt, 3, SQL_INTEGER, &uposy, 10, &cb_uposy);

						retcode = SQLFetch(hstmt);
						if (retcode == SQL_ERROR || retcode == SQL_SUCCESS_WITH_INFO)
							printf("error\n");
						if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
							wprintf(L"%S : %d %d\n", uid, uposx, uposy);
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


void send_packet(int client, void *packet) {
	char *p = reinterpret_cast<char *>(packet);

	OVER_EX *ov = new OVER_EX;
	ov->dataBuffer.len = p[0];
	ov->dataBuffer.buf = ov->messageBuffer;
	ov->event_t = EVT_SEND;
	memcpy(ov->messageBuffer, p, p[0]);
	ZeroMemory(&ov->over, sizeof(ov->over));

	// delete 해주지 않으면 메모리 누수 생기니까 해주자

	int error = WSASend(clients[client].socket, &ov->dataBuffer, 1, 0, 0, &ov->over, NULL);

	if (0 != error) {
		if (WSAGetLastError() != WSA_IO_PENDING)
		{
			cout << "Error - IO pending Failure\n";
			while (true);
		}
	}
	else {
		//		cout << "Non Overlapped Send return.\n";
	}

	//delete ov;
}

void send_chat_packet(int client, int from_id, wchar_t *msg) {
	sc_packet_chat packet;
	packet.id = from_id;
	packet.size = sizeof(packet);
	packet.type = SC_CHAT;
	wcscpy_s(packet.message, msg);
	send_packet(client, &packet);
}

void send_pos_packet(int client, int pl) {
	sc_packet_move_player packet;
	packet.id = pl;
	packet.size = sizeof(packet);
	packet.type = SC_MOVE_PLAYER;
	packet.x = clients[pl].x;
	packet.y = clients[pl].y;

	//cout << "send pos packet client " << client << '\n';
	//cout << clients[pl].x << ", " << clients[pl].y << '\n';

	send_packet(client, &packet);
}

void send_npc_pos_packet(int client, int npc) {
	sc_packet_move_player packet;
	packet.id = npc;
	packet.size = sizeof(packet);
	packet.type = SC_MOVE_NPC;
	packet.x = npcs[npc].x;
	packet.y = npcs[npc].y;

	send_packet(client, &packet);
}

void send_login_ok_packet(int new_id) {
	sc_packet_login_ok packet;
	packet.id = new_id;
	packet.size = sizeof(packet);
	packet.type = SC_LOGIN_OK;

	send_packet(new_id, &packet);
}

void send_remove_player_packet(int client, int id) {
	sc_packet_remove_player packet;
	packet.id = id;
	packet.size = sizeof(packet);
	packet.type = SC_REMOVE_PLAYER;

	send_packet(client, &packet);
}

void send_remove_npc_packet(int client, int npc) {
	sc_packet_remove_player packet;
	packet.id = npc;
	packet.size = sizeof(packet);
	packet.type = SC_REMOVE_NPC;

	send_packet(client, &packet);
}

void send_put_player_packet(int client, int new_id) {
	sc_packet_put_player packet;
	packet.id = new_id;
	packet.size = sizeof(packet);
	packet.type = SC_PUT_PLAYER;
	packet.x = clients[new_id].x;
	packet.y = clients[new_id].y;

	send_packet(client, &packet);
}

void send_put_npc_packet(int client, int npc) {
	sc_packet_put_player packet;
	packet.id = npc;
	packet.size = sizeof(packet);
	packet.type = SC_PUT_NPC;
	packet.x = npcs[npc].x;
	packet.y = npcs[npc].y;

	send_packet(client, &packet);
}

void process_packet(int client, char * packet) {
	if (!clients[client].is_login_ok) {
		cs_packet_id *idp = reinterpret_cast<cs_packet_id *>(packet);
		wcout << L"접속 요청 ID : " << idp->id << '\n';
		strcpy(clients[client].login_id, idp->id);
		wcout << L"ID : " << clients[client].login_id << '\n';
		add_db_evt(client, DB_EVT_SEARCH);
	}
	else {
		cs_packet_id *p = reinterpret_cast<cs_packet_id *>(packet);

		int x = clients[client].x;
		int y = clients[client].y;

		clients[client].view_lock.lock();
		auto old_vl = clients[client].viewlist;
		auto old_npc_vl = clients[client].npc_viewlist;
		clients[client].view_lock.unlock();

		switch (p->type) {
		case CS_UP: if (y > 0) y--; break;
		case CS_DOWN: if (y < (WORLD_HEIGHT - 1)) y++; break;
		case CS_LEFT: if (x > 0) x--; break;
		case CS_RIGHT: if (x < (WORLD_WIDTH - 1)) x++; break;
		default:
			wcout << L"정의되지 않은 패킷 도착 오류!!\n";
			//while (true);
		}

		clients[client].x = x;
		clients[client].y = y;

		unordered_set <int> new_vl; // 이동 후의 새로운 뷰리스트
		for (int i = 0; i < MAX_USER; ++i) {
			if (false == clients[i].in_use) continue;
			if (i == client) continue; // 나는 넘어가기
			if (false == is_eyesight(i, client)) continue; // 안보이면 추가안함
			new_vl.insert(i); // 이동 후에 보인 애들이 여기에 들어가있겠지.. 이걸로 비교하면 된다!
		}

		send_pos_packet(client, client);

		// 3가지 Case가 있다
		// Case 1. old_vl 에도 있고 new_vl에도 있는 플레이어 -> Send packet~
		for (auto &player : old_vl) {
			if (0 == new_vl.count(player)) continue; // 뉴 리스트에 없으면 넘어가~
			clients[player].view_lock.lock();
			if (0 < clients[player].viewlist.count(client)) { // 상대방 뷰리스트에 나도 있으면 걍 보내기~
				clients[player].view_lock.unlock();
				send_pos_packet(player, client);
			}
			else { // 나는 얘가 있는데 상대방 뷰리스트에 내가 없어.. 그럼 상대방 뷰리스트에 나를 추가하고 보내기
				clients[player].viewlist.insert(client);
				clients[player].view_lock.unlock();

				send_put_player_packet(player, client);
			}
		}

		// Case 2. old_vl 에 없음, new_vl 에는 있음 -> 내 시야에 들어온거임~
		for (auto &player : new_vl) {
			if (0 < old_vl.count(player)) continue; // 옛날 뷰리스트에 상대방이 있으면 통과~

			// 없으면 내 뷰리스트에 추가해주자
			clients[client].view_lock.lock();
			clients[client].viewlist.insert(player);
			clients[client].view_lock.unlock();

			send_put_player_packet(client, player);

			clients[player].view_lock.lock();
			// 상대방 뷰리스트에 내가 없으면 날 추가한다
			if (0 == clients[player].viewlist.count(client)) {
				clients[player].viewlist.insert(client);
				clients[player].view_lock.unlock();

				send_put_player_packet(player, client);
			}
			else {
				clients[player].view_lock.unlock();

				send_pos_packet(player, client);
			}
		}

		// Case 3. old_vl에 있었는데 new_vl에는 없는 경우 -> 내 시야에서 사라진 경우
		for (auto &player : old_vl) {
			if (0 < new_vl.count(player)) continue; // 새로운 리스트에 상대방 있으면 넘어가~

			clients[client].view_lock.lock();
			clients[client].viewlist.erase(player);
			clients[client].view_lock.unlock();

			send_remove_player_packet(client, player);

			clients[player].view_lock.lock();
			// 상대방 뷰 리스트에 여전히 내가 있다면 삭제해주기
			if (0 < clients[player].viewlist.count(client)) {
				clients[player].viewlist.erase(client);
				clients[player].view_lock.unlock();

				send_remove_player_packet(player, client);
			}
			else 	clients[player].view_lock.unlock();

		}


		// npc 뷰리스트
		unordered_set <int> new_npc_vl; // 이동 후의 새로운 뷰리스트
		for (int i = 0; i < MAX_NPC; ++i) {
			if (false == is_player_npc_eyesight(client, i)) continue;
			new_npc_vl.insert(i); // 이동 후에 보이는 npc들
		}


		// Case 1. old_vl, new_vl에 있는 npc
		for (auto &npc : old_npc_vl) {
			// 옛날 시야에는 있었지만 새로운 시야에 없으면
			if (0 == new_npc_vl.count(npc)) {
				clients[client].view_lock.lock();
				clients[client].npc_viewlist.erase(npc);
				clients[client].view_lock.unlock();

				send_remove_npc_packet(client, npc);
			}
		}

		// Case 2. old_vl 에 없음, new_vl 에는 있음 -> 내 시야에 들어온거임~
		for (auto &npc : new_npc_vl) {
			if (0 < old_npc_vl.count(npc)) continue; // 옛날 뷰리스트에 있으면 통과~

			// 없으면 내 뷰리스트에 추가해주자
			clients[client].view_lock.lock();
			clients[client].npc_viewlist.insert(npc);
			clients[client].view_lock.unlock();

			send_put_npc_packet(client, npc);

			wakeup_NPC(npc);
		}

		for (auto &monster : new_npc_vl) {
			OVER_EX *ex_over = new OVER_EX;
			ex_over->event_t = EVT_PLAYER_MOVE_DETECT;
			ex_over->target_player = client;

			PostQueuedCompletionStatus(g_iocp, 1, monster, &ex_over->over);
		}
	}
}

void process_event(EVENT_ST &ev) {

	bool player_is_near = false;
	switch (ev.type) {
	case EVT_MOVE:
		player_is_near = false;
		for (int i = 0; i < MAX_USER; ++i) {
			if (false == clients[i].in_use) continue;
			if (false == is_npc_eyesight(i, ev.obj_id)) continue;

			player_is_near = true;
			break;
		}

		if (player_is_near) {
			move_npc(ev.obj_id);
			add_timer(ev.obj_id, EVT_MOVE, high_resolution_clock::now() + 1s);
		}
		else {
			npcs[ev.obj_id].is_sleeping = true;
		}
		break;
	case EVT_HEAL: break;
	case EVT_RECV: break;
	case EVT_SEND:break;

	default:
		break;
	}
}
void process_db_event(DB_EVENT_ST &ev) {
	int new_id = ev.client_id;
	switch (ev.type) {
	case DB_EVT_SEARCH:
		if (search_user_id(ev.client_id))
		{
			send_login_ok_packet(new_id);
			send_put_player_packet(new_id, new_id); // 나한테 내 위치 보내기

			// 다른 플레이어의 뷰리스트에 나를 추가하고 전송
			for (int i = 0; i < MAX_USER; ++i) {
				if (false == clients[i].in_use) continue;
				if (false == is_eyesight(new_id, i)) continue;
				if (i == new_id) continue;

				clients[i].view_lock.lock();
				clients[i].viewlist.insert(new_id);
				clients[i].view_lock.unlock();

				send_put_player_packet(i, new_id);
			}


			// 내 위치를 뷰리스트에 있는 다른 플레이어에게 전송
			for (int i = 0; i < MAX_USER; ++i) {
				if (false == clients[i].in_use) continue;
				if (i == new_id) continue;
				if (false == is_eyesight(i, new_id)) continue;

				clients[new_id].view_lock.lock();
				clients[new_id].viewlist.insert(i);
				clients[new_id].view_lock.unlock();

				send_put_player_packet(new_id, i);
			}


			// 내 주변에 있는 NPC를 뷰리스트에 추가
			for (int i = 0; i < MAX_NPC; ++i) {
				if (false == is_player_npc_eyesight(new_id, i)) continue;

				clients[new_id].view_lock.lock();
				clients[new_id].npc_viewlist.insert(i);
				clients[new_id].view_lock.unlock();

				send_put_npc_packet(new_id, i);

				wakeup_NPC(i);
			}

		}
		else 	disconnect_client(new_id);


		break;
	case DB_EVT_SAVE:
		if (save_user_data(ev.client_id)) {
			wcout << L"저장 성공!\n";
		}
		else {
			wcout << L"저장 실패!!!\n";
		}
		wcout << L"연결을 종료합니다! \n";
		clients[new_id].is_login_ok = false;
		disconnect_client(new_id);

		break;
	default:
		break;
	}
}

void disconnect_client(int id) {
	if (clients[id].is_login_ok) add_db_evt(id, DB_EVT_SAVE);
	else {
		for (int i = 0; i < MAX_USER; ++i) {
			if (false == clients[i].in_use) continue;
			if (i == id) continue;
			clients[i].view_lock.lock();
			if (0 == clients[i].viewlist.count(id)) {
				clients[i].view_lock.unlock();
				continue; // 다른 클라이언트의 뷰리스트에 내가 없으면 넘어가기
			}
			clients[i].viewlist.erase(id);
			clients[i].view_lock.unlock();

			send_remove_player_packet(i, id);
		}
		closesocket(clients[id].socket);
		clients[id].in_use = false;
		clients[id].is_login_ok = false;

		clients[id].view_lock.lock();
		clients[id].viewlist.clear();
		clients[id].view_lock.unlock();
	}
}

void worker_thread() {

	while (true) {
		DWORD io_byte;
		ULONGLONG l_key;
		OVER_EX *over_ex;


		// IOCP는 worker_thread가 recv용, send용이 따로 나뉘지 않는다.. 그럼 어떻게 구분하냐..?
		// 우리가 만든 오버랩드 확장 구조체의 포인터인 over를 통해서 알수있다!!~~!~!!!
		int is_error = GetQueuedCompletionStatus(g_iocp, &io_byte, &l_key, reinterpret_cast<LPOVERLAPPED *>(&over_ex), INFINITE);

		int key = static_cast<int>(l_key);

		// 에러 2가지 경우
		// 1. 클라가 closesocket하지 않고 종료한 경우
		if (0 == is_error) {
			int err_no = WSAGetLastError();
			if (64 == err_no) {
				disconnect_client(key);
				continue;
			}
			else err_display("GQCS : ", err_no);
		}

		// 2. 클라가 closesocket하고 종료한 경우
		if (0 == io_byte) {
			disconnect_client(key);
		}


		if (EVT_RECV == over_ex->event_t) {
			// RECV
//			wcout << "Packet from Client : " << key << endl;

			// 패킷 조립
			int rest = io_byte;
			char *ptr = over_ex->messageBuffer;
			char packet_size = 0;

			if (0 < clients[key].prev_size) {
				packet_size = clients[key].packetBuffer[0];
			}

			while (0 < rest) {
				// 패킷사이즈가 0이되서 넘어왔다? 그럼 만들어줘야지..
				if (0 == packet_size) {
					packet_size = ptr[0];
				}
				// 패킷을 완성하려면 앞으로 몇 바이트 더 받아와야하냐?
				int required = packet_size - clients[key].prev_size;

				// 패킷을 만들 수 있으면
				if (required <= rest) {
					// 앞에 이전 데이터가 존재한다면 덮어쓰게 되니까 추가하는거임
					memcpy(clients[key].packetBuffer + clients[key].prev_size, ptr, required);
					process_packet(key, clients[key].packetBuffer);
					rest -= required;
					ptr += required;
					packet_size = 0;
					clients[key].prev_size = 0;
				}
				else {
					memcpy(clients[key].packetBuffer + clients[key].prev_size, ptr, rest);
					rest = 0;
					clients[key].prev_size += rest;
				}
			}
			do_recv(key);
		}
		else if (EVT_SEND == over_ex->event_t) {
			if (false == over_ex->event_t)
				delete over_ex;
		}
		else if (EVT_MOVE == over_ex->event_t) {
			EVENT_ST ev;
			ev.obj_id = key;
			ev.start_time = high_resolution_clock::now();
			ev.type = EVT_MOVE;

			//	cout << "EVT_MOVE " << ev.obj_id << '\n';
			process_event(ev);
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
		else if (EVT_PLAYER_MOVE_DETECT == over_ex->event_t) {
			lua_State *L = npcs[key].L;
			int player_id = over_ex->target_player;

			// 워커스레드가 돌리기때문에 락이 필요하다
			npcs[key].lua_lock.lock();
			lua_getglobal(L, "event_player_move");
			lua_pushnumber(L, player_id);
			lua_pcall(L, 1, 0, 0);
			npcs[key].lua_lock.unlock();
			delete over_ex;
		}
		else {
			cout << "UNKNOWN EVENT\n";
			while (true);
		}
	}
}

void do_timer() {
	while (true) {
		this_thread::sleep_for(10ms);
		while (true) {
			timer_lock.lock();
			if (true == timer_queue.empty()) {
				timer_lock.unlock();
				break;
			}

			EVENT_ST ev = timer_queue.top();

			if (ev.start_time < high_resolution_clock::now()) {
				timer_lock.unlock();
				break;
			}


			timer_queue.pop();
			timer_lock.unlock();

			OVER_EX *over_ex = new OVER_EX;
			over_ex->event_t = EVT_MOVE;
			PostQueuedCompletionStatus(g_iocp, 1, ev.obj_id, &over_ex->over);
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

void move_npc(int npc_id) {
	int x = npcs[npc_id].x;
	int y = npcs[npc_id].y;

	// npc의 시야에 들어오는 플레이어~~
	unordered_set<int> old_vl;
	for (int i = 0; i < MAX_USER; ++i) {
		if (false == clients[i].in_use) continue;
		if (false == is_npc_eyesight(i, npc_id)) continue;
		old_vl.insert(i);
	}

	//	cout << "move npc\n";

	switch (rand() % 4 + 1) {
	case 1: if (y > 0) y--; break;
	case 2: if (y < (WORLD_HEIGHT - 1)) y++; break;
	case 3: if (x > 0) x--; break;
	case 4: if (x < (WORLD_WIDTH - 1)) x++; break;
	default: break;
	}

	npcs[npc_id].x = x;
	npcs[npc_id].y = y;

	unordered_set<int> new_vl;

	// 이동 후 npc 시야에 들어오는 플레이어~~
	for (int i = 0; i < MAX_USER; ++i) {
		if (false == clients[i].in_use) continue;
		if (false == is_npc_eyesight(i, npc_id)) continue;
		new_vl.insert(i);
	}

	// 새로 만난 플레이어, 계속 있는 플레이어 처리
	for (auto &pl : new_vl) {
		// 만약 이동 전 뷰리스트에 플레이어가 없다면
		if (0 == old_vl.count(pl)) {
			// 그 플레이어 뷰리스트에 내가 있는지 확인하고 없으면 넣어주기
			clients[pl].view_lock.lock();
			if (0 == clients[pl].npc_viewlist.count(npc_id)) {
				clients[pl].npc_viewlist.insert(npc_id);
				clients[pl].view_lock.unlock();

				send_put_npc_packet(pl, npc_id);
			}
			else {
				clients[pl].view_lock.unlock();

				send_npc_pos_packet(pl, npc_id);
			}
		}
		else send_npc_pos_packet(pl, npc_id);
	}

	// 헤어진 플레이어 처리
	for (auto &pl : old_vl) {
		// 새로운 뷰리스트에 없는 플레이어는
		if (0 == new_vl.count(pl)) {
			clients[pl].view_lock.lock();

			// 그 플레이어의 뷰리스트에 내가 있는지 확인하고 있으면 지워주기
			if (0 < clients[pl].npc_viewlist.count(npc_id)) {
				clients[pl].view_lock.unlock();

				clients[pl].npc_viewlist.erase(npc_id);
				send_remove_npc_packet(pl, npc_id);
			}
			else 	clients[pl].view_lock.unlock();
		}
	}

}

void wakeup_NPC(int npc_id) {
	if (true == is_sleeping(npc_id)) {
		npcs[npc_id].is_sleeping = false;
		EVENT_ST ev;

		ev.obj_id = npc_id;
		ev.type = EVT_MOVE;
		ev.start_time = high_resolution_clock::now() + 1s;

		add_timer(ev.obj_id, ev.type, ev.start_time);
	}
}

bool is_eyesight(int client, int other_client) {
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

	if (distance < (NPC_RADIUS * NPC_RADIUS)) return true;
	else return false;
}

bool is_sleeping(int npc_id) {
	return npcs[npc_id].is_sleeping;
}
