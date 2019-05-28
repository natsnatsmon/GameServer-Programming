#include <iostream>
#include <unordered_set>
#include <vector>
#include <queue>
#include <thread>
using namespace std;
#include <winsock2.h>
#include "protocol.h"
#include <chrono>
#include <mutex>
using namespace chrono;


#pragma comment(lib, "Ws2_32.lib")

#define MAX_BUFFER        1024
#define VIEW_RADIUS       5   

enum EVENT_TYPE { EVT_MOVE, EVT_HEAL, EVT_RECV, EVT_SEND };

struct OVER_EX {
	WSAOVERLAPPED	over;
	WSABUF dataBuffer;
	char messageBuffer[MAX_BUFFER];
	EVENT_TYPE			is_recv;
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
	unordered_set <int> view_list;

	bool is_sleeping;

	SOCKETINFO() {
		in_use = false;
		over_ex.dataBuffer.len = MAX_BUFFER;
		over_ex.dataBuffer.buf = over_ex.messageBuffer;
		over_ex.is_recv = EVT_RECV;
	}
};

struct EVENT_ST {
	int obj_id;
	EVENT_TYPE type;
	high_resolution_clock::time_point start_time;

	constexpr bool operator < (const EVENT_ST& _Left) const
	{	// apply operator< to operands
		return (start_time < _Left.start_time);
	}
};

mutex timer_lock;
// 우선순위 큐
priority_queue<EVENT_ST> timer_queue;
HANDLE g_iocp;

SOCKETINFO clients[MAX_USER + NUM_NPC];

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

void Initialize_PC() {
	for (int i = 0; i < MAX_USER; ++i) {
		clients[i].in_use = false;
		clients[i].view_list.clear();
	}
}

void Add_Timer(int obj_id, EVENT_TYPE et, high_resolution_clock::time_point start_time) {
	timer_lock.lock();
	timer_queue.emplace(EVENT_ST{ obj_id, et, start_time });
	timer_lock.unlock();
		
}
void Initialize_NPC()
{
	for (int i = 0; i < NUM_NPC; ++i) {
		int npc_id = i + MAX_USER;
		clients[npc_id].in_use = true;
		clients[npc_id].x = rand() % WORLD_WIDTH;
		clients[npc_id].y = rand() % WORLD_HEIGHT;
		clients[npc_id].is_sleeping = true;

		Add_Timer(npc_id, EVT_MOVE, high_resolution_clock::now() + 1s);
	}
}

bool Is_Near_Object(int a, int b)
{
	if (VIEW_RADIUS < abs(clients[a].x - clients[b].x)) 
		return false;
	if (VIEW_RADIUS < abs(clients[a].y - clients[b].y))
		return false;
	return true;
}

bool Is_NPC(int id) {
	if ((id >= 
MAX_USER) && (id < MAX_USER + NUM_NPC))
		return true;
	else
		return false;
}

bool Is_Sleeping(int id) {
	return 	clients[id].is_sleeping;
}

void wakeup_NPC(int id) {
	if (true == Is_Sleeping(id)) {
		clients[id].is_sleeping = false;
		EVENT_ST ev;
		ev.obj_id = id;
		ev.type = EVT_MOVE;
		ev.start_time = high_resolution_clock::now() + 1s;
		
		timer_lock.lock();
		timer_queue.push(ev);
		timer_lock.unlock();

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
	ov->is_recv = EVT_SEND;
	memcpy(ov->messageBuffer, p, p[0]);
	ZeroMemory(&ov->over, sizeof(ov->over));
	int error = WSASend(clients[client].socket, &ov->dataBuffer, 1, 0, 0,
		&ov->over, NULL);
	if (0 != error) {
		if (WSAGetLastError() != WSA_IO_PENDING)
		{
			cout << "Error - IO pending Failure\n";
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

void process_packet(int client, char *packet)
{
	cs_packet_up *p = reinterpret_cast<cs_packet_up *>(packet);
	short x = clients[client].x;
	short y = clients[client].y;

	auto old_vl = clients[client].view_list;
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
		if (false == clients[i].in_use) continue;
		if (i == client) continue;
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
		if (true == Is_NPC(pl)) continue;

		if (0 < clients[pl].view_list.count(client))
			send_pos_packet(pl, client);
		else {
			clients[pl].view_list.insert(client);
			send_put_player_packet(pl, client);
		}
	}
	// 2. old_vl에 없고 new_vl에만 있는 플레이어
	for (auto pl : new_vl) {
		if (0 < old_vl.count(pl)) continue;
		clients[client].view_list.insert(pl);
		send_put_player_packet(client, pl);

		if (true == Is_NPC(pl)) {
			wakeup_NPC(pl);
			continue;
		};

		if (0 == clients[pl].view_list.count(client)) {
			clients[pl].view_list.insert(client);
			send_put_player_packet(pl, client);
		} else
			send_pos_packet(pl, client);
	}
	// 3. old_vl에 있고 new_vl에는 없는 플레이어
	for (auto pl : old_vl) {
		if (0 < new_vl.count(pl)) continue;
		clients[client].view_list.erase(pl);
		send_remove_player_packet(client, pl);
		if (true == Is_NPC(pl)) continue;
		if (0 < clients[pl].view_list.count(client)) {
			clients[pl].view_list.erase(client);
			send_remove_player_packet(pl, client);
		}
	}

}

void disconnect_client(int id)
{
	for (int i = 0; i < MAX_USER; ++i) {
		if (false == clients[i].in_use) continue;
		if (i == id) continue;
		if (0 == clients[i].view_list.count(id)) continue;
		clients[i].view_list.erase(id);
		send_remove_player_packet(i, id);
	}
	closesocket(clients[id].socket);
	clients[id].in_use = false;
	clients[id].view_list.clear();
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
		char key = static_cast<char>(l_key);
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


		if (EVT_RECV == over_ex->is_recv) {
			wcout << "Packet from Client:" << key << endl;
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
		else {
			if (EVT_RECV != over_ex->is_recv)
			delete over_ex;
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
		clients[new_id].x = clients[new_id].y = 4;
		clients[new_id].view_list.clear();
		ZeroMemory(&clients[new_id].over_ex.over,
			sizeof(clients[new_id].over_ex.over));
		flags = 0;

		CreateIoCompletionPort(reinterpret_cast<HANDLE>(clientSocket),
			g_iocp, new_id, 0);

		clients[new_id].in_use = true;

		send_login_ok_packet(new_id);
		send_put_player_packet(new_id, new_id);
		for (int i = 0; i < MAX_USER; ++i) {
			if (false == clients[i].in_use) continue;
			if (false == Is_Near_Object(new_id, i)) continue;
			if (i == new_id) continue;
			clients[i].view_list.insert(new_id);
			send_put_player_packet(i, new_id);
		}

		for (int i = 0; i < MAX_USER + NUM_NPC; ++i) {
			if (false == clients[i].in_use) continue;
			if (i == new_id) continue;
			if (false == Is_Near_Object(i, new_id)) continue;
			if (true == Is_NPC(i)) wakeup_NPC(i);
			clients[new_id].view_list.insert(i);
			send_put_player_packet(new_id, i);
		}
		do_recv(new_id);
	}

	// 6-2. 리슨 소켓종료
	closesocket(listenSocket);

	// Winsock End
	WSACleanup();

	return 0;
}

void random_move_NPC(int id) {
	int x = clients[id].x;
	int y = clients[id].y;
	
	unordered_set<int> old_vl;
	for (int i = 0; i < MAX_USER; ++i) {
		if (false == clients[i].in_use) continue;
		if (false == Is_Near_Object(id, i)) continue;
		old_vl.insert(i);
	}

	switch (rand() % 4) {
	case 0: if(x > 0) x--; break;
	case 1: if (x < (WORLD_WIDTH - 1)) x++; break;
	case 2: if (y > 0) y--; break;
	case 3: if (y < (WORLD_HEIGHT - 1)) y++; break;
	default: break;
	}

	clients[id].x = x;
	clients[id].y = y;

	unordered_set<int> new_vl;
	for (int i = 0; i < MAX_USER; ++i) {
		if (false == clients[i].in_use) continue;
		if (false == Is_Near_Object(id, i)) continue;
		new_vl.insert(i);
	}

	// 새로 만난 플레이어 처리, 이미 내가 뷰리스트에 있는 플레이어 처리
	for (auto pl : new_vl) {
		if (0 == clients[pl].view_list.count(pl)) {
			clients[pl].view_list.insert(id);
			send_put_player_packet(pl, id);
		}
		else send_pos_packet(pl, id);
	}

	// 헤어진 플레이어 처리
	for (auto pl : old_vl) {
		if (0 == new_vl.count(pl)) {
			if (0 < clients[pl].view_list.count(pl)) {
				clients[pl].view_list.erase(id);
				send_remove_player_packet(pl, id);
			}
		}
	}
}

int do_AI() {
	while (true) {
		this_thread::sleep_for(1s);

		auto ai_start_t = high_resolution_clock::now();

		// 이방법은 타임복잡도가 N제곱이다.. N2 N2~!~!!~!~!!!
		for (int i = MAX_USER; i < MAX_USER + NUM_NPC; ++i) {
			bool need_move = false;
			for (int j = 0; j < MAX_USER; ++i) {
				if (i == j) continue;
				if (false == clients[j].in_use) continue;
				if (false == Is_Near_Object(i, j)) continue;
				need_move = true;
			}
			if(need_move)
				random_move_NPC(i);
		}
		auto ai_end_t = high_resolution_clock::now();

		auto ai_time = ai_end_t - ai_start_t;

		cout << "AI Processing Time = " << duration_cast<milliseconds>(ai_time).count() << "ms \n";
	}

	return 0;
}

void process_event(EVENT_ST &ev) {
	EVENT_ST new_ev = ev;
	bool player_is_near = false;

	switch (ev.type) {
	case EVT_MOVE:
		new_ev = ev;
		cout << ev.obj_id << endl;
		player_is_near = false;
		for (int i = 0; i < MAX_USER; ++i) {
			if (false == clients[i].in_use) continue;
			if (false == Is_Near_Object(i, ev.obj_id)) continue;
			player_is_near = true;
			break;
		}

		if (player_is_near) {
			random_move_NPC(ev.obj_id);
			Add_Timer(ev.obj_id, EVT_MOVE, high_resolution_clock::now() + 1s);
		}
		else {
			clients[ev.obj_id].is_sleeping = true;
		}
		break;
	case EVT_HEAL: break;
	case EVT_RECV: break;
	case EVT_SEND:break;
	default :
		cout << "Unknown Event Error!\n";
	}
}

void do_timer() {
	while (true) {
		this_thread::sleep_for(10ms); // 이중 while로 안돌리고 while 하나에 슬맆넣으면 이벤트 100개 처리하는데 무조건 1초 이상 걸림.. 이벤트 하나하고 10쉬고.. ㅋㄱㅋㅋㅋ
		
		while (true) {
			// 꺼내기 전에 empty인지 아닌지 확인 해줘야 안터진다~~
			timer_lock.lock();
			if (true == timer_queue.empty()) {
				timer_lock.unlock();
				break;
			}


			EVENT_ST ev = timer_queue.top();
			
			//cout << ev.obj_id << endl;

			if (ev.start_time > high_resolution_clock::now()) {
				timer_lock.unlock();
				break;
			}

			timer_queue.pop();
			timer_lock.unlock();
			process_event(ev);
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
	thread timer_thread{ do_timer };
	
	accept_thread.join();
	timer_thread.join();
//	thread npc_thread{ do_AI };
//	npc_thread.join();

	for (auto &th : worker_threads) th.join();
}