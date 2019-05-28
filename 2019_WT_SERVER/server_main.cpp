/*
## ���� ���� : 1 v n - overlapped callback
1. socket()            : ���ϻ���
2. bind()            : ���ϼ���
3. listen()            : ���Ŵ�⿭����
4. accept()            : ������
5. read()&write()
WIN recv()&send    : ������ �а���
6. close()
WIN closesocket    : ��������
*/

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


#pragma comment(lib, "Ws2_32.lib")

#define MAX_BUFFER        1024
#define VIEW_RADIUS			8
#define NPC_RADIUS			9
enum EVENT_TYPE { EVT_MOVE, EVT_HEAL, EVT_RECV, EVT_SEND };

// �������� ����ü�� Ȯ���ؼ� ����
struct OVER_EX{
	WSAOVERLAPPED over;
	WSABUF dataBuffer;
	char messageBuffer[MAX_BUFFER];
	EVENT_TYPE event_t;
};

class SOCKETINFO
{
public:
//	mutex access_lock;
	bool in_use;
	// Ŭ���̾�Ʈ���� �ϳ��� �־�ߴ�.. �ϳ��� �����ϸ� ���������°Ŵ�... �ؿ� 4���� Ŭ�󸶴� �� �־�� �ϴ� ��.. ����驼�
	unordered_set <int> viewlist;
	unordered_set <int> npc_viewlist;
	mutex myLock;
	OVER_EX over_ex;
	SOCKET socket;
	char packetBuffer[MAX_BUFFER];
	int prev_size;
	int x, y;

	SOCKETINFO() {
		in_use = false;
		over_ex.dataBuffer.len = MAX_BUFFER;
		over_ex.dataBuffer.buf = over_ex.messageBuffer;
		over_ex.event_t = EVT_RECV;
	}
};
SOCKETINFO clients[MAX_USER];

// NPC ���� ù��°
class NPCINFO {
public :
	int x, y;
	bool is_sleeping;
};
NPCINFO npcs[MAX_NPC];
// ���� : �޸��� ���� ����. �������̴�.
// ���� : �Լ��� �ߺ� ������ �ʿ��ϴ�.
// // bool Is_Near_Object(int a, int b); �� �Լ� �ϳ���
// // bool Is_Near_Player_Player, Is_Near_Player_Npc, Is_Near_Npc_Npc;
 
 
//// NPC ���� �ι�°
// class NPCINFO {
// public :
// 	char x, y;
// };
// 
// class SOCKETINFO : public NPCINFO {
// 	bool in_use;
// 	mutex myLock;
// 	OVER_EX over_ex;
// 	SOCKET socket;
// 	char packetBuffer[MAX_BUFFER];
// 	int prev_size;
// 	unordered_set <int> viewlist;
// 	
// 	SOCKETINFO() {
// 		in_use = false;
// 		over_ex.dataBuffer.len = MAX_BUFFER;
// 		over_ex.dataBuffer.buf = over_ex.messageBuffer;
// 		over_ex.is_recv = true;
// 	}
// };
// 
// NPCINFO *objects[MAX_USER + MAX_NPC];
 
 // ���� : �޸��� ���� ����. �Լ��� �ߺ� ������ �ʿ� ����.
 // ���� : �������� ���. ��������
 // Ư¡ : ID ����� �ϰų�, object_type ����� �ʿ�. (�߰����� ���⵵�� �þ)

 // NPC ���� �ǽ� �ܼ� ����
// ID : 0 ~ 9 �÷��̾�
// ID : 10 ~ 10 + NUM_NPC - 1 ������ NPC
// SOCKETINFO clients[MAX_USER + MAX_NPC];
// ���� : ������ ���X
// ���� : ���� �޸� ����

struct EVENT_ST {
	int obj_id;
	EVENT_TYPE type;
	high_resolution_clock::time_point start_time;
	constexpr bool operator < (const EVENT_ST& _Left)const {
		return(start_time > _Left.start_time);
	}
};

mutex timer_lock;

priority_queue<EVENT_ST> timer_queue;

HANDLE g_iocp;

void err_display(const char * msg, int err_no);
void add_timer(int obj_id, EVENT_TYPE et, high_resolution_clock::time_point start_time);
void Init();

int do_accept();
void do_timer();
void do_recv(int id);

void send_packet(int client, void *packet);

void send_pos_packet(int client, int pl);
void send_npc_pos_packet(int client, int npc);

void send_login_ok_packet(int new_id);

void send_remove_player_packet(int clinet, int id);
void send_remove_npc_packet(int client, int npc);

void send_put_player_packet(int clients, int new_id);
void send_put_npc_packet(int clients, int npc);

void process_packet(int client, char *packet);
void process_event(EVENT_ST &ev);

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
	accept_thread.join();
	timer_thread.join();

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
	wcout << L"����  [" << err_no << L"] " << lpMsgBuf << endl;
	while (true);
	LocalFree(lpMsgBuf);
}

void add_timer(int obj_id, EVENT_TYPE et, high_resolution_clock::time_point start_time) {
	timer_lock.lock();
	timer_queue.emplace(EVENT_ST{ obj_id,et,start_time });
	timer_lock.unlock();
}

void Init() {
	for (int pl_id = 0; pl_id < MAX_USER; ++pl_id) {
		clients[pl_id].in_use = false;
	}

	for (int npc_id = 0; npc_id < MAX_NPC; ++npc_id) {
		npcs[npc_id].x = rand() % WORLD_WIDTH;
		npcs[npc_id].y = rand() % WORLD_HEIGHT;
		npcs[npc_id].is_sleeping = true;
		//add_timer(npc_id, EVT_MOVE, high_resolution_clock::now() + 1s);
	}
}

int do_accept()
{
	// Winsock Start - winsock.dll �ε�
	WSADATA WSAData;
	if (WSAStartup(MAKEWORD(2, 2), &WSAData) != 0)
	{
		cout << "Error - Can not load 'winsock.dll' file\n";
		return 1;
	}

	// 1. ���ϻ���  
	SOCKET listenSocket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (listenSocket == INVALID_SOCKET)
	{
		cout << "Error - Invalid socket\n";
		return 1;
	}

	// �������� ��ü����
	SOCKADDR_IN serverAddr;
//	memset(&serverAddr, 0, sizeof(SOCKADDR_IN));
	serverAddr.sin_family = PF_INET;
	serverAddr.sin_port = htons(SERVER_PORT);
	serverAddr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);

	// 2. ���ϼ���
	if (::bind(listenSocket, (struct sockaddr*)&serverAddr, sizeof(SOCKADDR_IN)) == SOCKET_ERROR)
	{
		cout << "Error - Fail bind\n";
		// 6. ��������
		closesocket(listenSocket);
		// Winsock End
		WSACleanup();
		return 1;
	}

	// 3. ���Ŵ�⿭����
	if (listen(listenSocket, 5) == SOCKET_ERROR)
	{
		cout << "Error - Fail listen\n";
		// 6. ��������
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
		// listenSocket�� �񵿱� ������ ����� clientSocket�� �񵿱Ⱑ �ȴ�!
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

		// recv �غ�
		clients[new_id].socket = clientSocket;
		clients[new_id].prev_size = 0;
		clients[new_id].x = clients[new_id].y = 400;
		clients[new_id].viewlist.clear(); // �丮��Ʈ �ʱ�ȭ
		ZeroMemory(&clients[new_id].over_ex.over, 
			sizeof(clients[new_id].over_ex.over));
		flags = 0;

		CreateIoCompletionPort(reinterpret_cast<HANDLE>(clientSocket), g_iocp, new_id, 0);

		clients[new_id].in_use = true; // IOCP�� ��� �� ���� true�� �ٲ���� �����͸� ���� �� �ִ�

		send_login_ok_packet(new_id);
		send_put_player_packet(new_id, new_id); // ������ �� ��ġ ������

		// �ٸ� �÷��̾��� �丮��Ʈ�� ���� �߰��ϰ� ����
		for (int i = 0; i < MAX_USER; ++i) {
			if (false == clients[i].in_use) continue;
			if (false == is_eyesight(new_id, i)) continue;
			if (i == new_id) continue;

			clients[i].myLock.lock();
			clients[i].viewlist.insert(new_id);
			clients[i].myLock.unlock();
			
			send_put_player_packet(i, new_id);
		}


		// �� ��ġ�� �丮��Ʈ�� �ִ� �ٸ� �÷��̾�� ����
		for (int i = 0; i < MAX_USER; ++i) {
			if (false == clients[i].in_use) continue;
			if (i == new_id) continue;
			if (false == is_eyesight(i, new_id)) continue;

			clients[new_id].myLock.lock();
			clients[new_id].viewlist.insert(i);
			clients[new_id].myLock.unlock();

			send_put_player_packet(new_id, i);
		}


		// �� �ֺ��� �ִ� NPC�� �丮��Ʈ�� �߰�
		for (int i = 0; i < MAX_NPC; ++i) {
			if (false == is_player_npc_eyesight(new_id, i)) continue;

			clients[new_id].myLock.lock();
			clients[new_id].npc_viewlist.insert(i);
			clients[new_id].myLock.unlock();

			send_put_npc_packet(new_id, i);

			wakeup_NPC(i);
		}

		do_recv(new_id);
	}

	// 6-2. ���� ��������
	closesocket(listenSocket);

	// Winsock End
	WSACleanup();

	return 0;
}

void do_recv(int id) {
	DWORD flags = 0;

	// ����, ���� �ּ�, ũ��, �ѹ� ���� ���ú� ��¼��, �÷���, �� Ŭ�� �������� �������� �������� ����ü, �ݹ��Լ� ������..
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

void send_packet(int client, void *packet) {
	char *p = reinterpret_cast<char *>(packet);

	OVER_EX *ov = new OVER_EX;
	ov->dataBuffer.len = p[0];
	ov->dataBuffer.buf = ov->messageBuffer;
	ov->event_t = EVT_SEND;
	memcpy(ov->messageBuffer, p, p[0]);
	ZeroMemory(&ov->over, sizeof(ov->over));

	// delete ������ ������ �޸� ���� ����ϱ� ������
	
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
	cs_packet_up *p = reinterpret_cast<cs_packet_up *>(packet);

	int x = clients[client].x;
	int y = clients[client].y;

	auto old_vl = clients[client].viewlist;
	auto old_npc_vl = clients[client].npc_viewlist;

	switch (p->type) {
	case CS_UP: if(y > 0) y--; break;
	case CS_DOWN: if(y < (WORLD_HEIGHT - 1)) y++; break;
	case CS_LEFT: if(x > 0) x--; break;
	case CS_RIGHT: if (x < (WORLD_WIDTH - 1)) x++; break;
	default:
		wcout << L"���ǵ��� ���� ��Ŷ ���� ����!!\n";
		while (true);
	}

	clients[client].x = x;
	clients[client].y = y;

	unordered_set <int> new_vl; // �̵� ���� ���ο� �丮��Ʈ
	for (int i = 0; i < MAX_USER; ++i) {
		if (false == clients[i].in_use) continue;
		if (i == client) continue; // ���� �Ѿ��
		if (false == is_eyesight(i, client)) continue; // �Ⱥ��̸� �߰�����
		new_vl.insert(i); // �̵� �Ŀ� ���� �ֵ��� ���⿡ ���ְ���.. �̰ɷ� ���ϸ� �ȴ�!
	}
	
	send_pos_packet(client, client);

	// 3���� Case�� �ִ�
	// Case 1. old_vl ���� �ְ� new_vl���� �ִ� �÷��̾� -> Send packet~
	for (auto &player : old_vl) {
		if (0 == new_vl.count(player)) continue; // �� ����Ʈ�� ������ �Ѿ~
		if (0 < clients[player].viewlist.count(client)) { // ���� �丮��Ʈ�� ���� ������ �� ������~
			send_pos_packet(player, client);
		}
		else { // ���� �갡 �ִµ� ���� �丮��Ʈ�� ���� ����.. �׷� ���� �丮��Ʈ�� ���� �߰��ϰ� ������
			clients[player].myLock.lock();
			clients[player].viewlist.insert(client);
			clients[player].myLock.unlock();

			send_put_player_packet(player, client);
		}
	}

	// Case 2. old_vl �� ����, new_vl ���� ���� -> �� �þ߿� ���°���~
	for (auto &player : new_vl) {
		if (0 < old_vl.count(player)) continue; // ���� �丮��Ʈ�� ������ ������ ���~

		// ������ �� �丮��Ʈ�� �߰�������
		clients[client].myLock.lock();
		clients[client].viewlist.insert(player);
		clients[client].myLock.unlock();

		send_put_player_packet(client, player);

		// ���� �丮��Ʈ�� ���� ������ �� �߰��Ѵ�
		if (0 == clients[player].viewlist.count(client)) {
			clients[player].myLock.lock();
			clients[player].viewlist.insert(client);
			clients[player].myLock.unlock();

			send_put_player_packet(player, client);
		}
		else {
			send_pos_packet(player, client);
		}
	}

	// Case 3. old_vl�� �־��µ� new_vl���� ���� ��� -> �� �þ߿��� ����� ���
	for (auto &player : old_vl) {
		if (0 < new_vl.count(player)) continue; // ���ο� ����Ʈ�� ���� ������ �Ѿ~

		clients[client].myLock.lock();
		clients[client].viewlist.erase(player);
		clients[client].myLock.unlock();

		send_remove_player_packet(client, player);

		// ���� �� ����Ʈ�� ������ ���� �ִٸ� �������ֱ�
		if (0 < clients[player].viewlist.count(client)) {
			clients[player].myLock.lock();
			clients[player].viewlist.erase(client);
			clients[player].myLock.unlock();

			send_remove_player_packet(player, client);
		}
	}


	// npc �丮��Ʈ
	unordered_set <int> new_npc_vl; // �̵� ���� ���ο� �丮��Ʈ
	for (int i = 0; i < MAX_NPC; ++i) {
		if (false == is_player_npc_eyesight(client, i)) continue;
		new_npc_vl.insert(i); // �̵� �Ŀ� ���̴� npc��
	}


	// Case 1. old_vl, new_vl�� �ִ� npc
	for (auto &npc : old_npc_vl) {
		// ���� �þ߿��� �־����� ���ο� �þ߿� ������
		if (0 == new_npc_vl.count(npc)) {
			clients[client].myLock.lock();
			clients[client].npc_viewlist.erase(npc);
			clients[client].myLock.unlock();

			send_remove_npc_packet(client, npc);
		}
//		else { 
//			send_put_npc_packet(client, npc);
//		}
	}

	// Case 2. old_vl �� ����, new_vl ���� ���� -> �� �þ߿� ���°���~
	for (auto &npc : new_npc_vl) {
		if (0 < old_npc_vl.count(npc)) continue; // ���� �丮��Ʈ�� ������ ���~

		// ������ �� �丮��Ʈ�� �߰�������
		clients[client].myLock.lock();
		clients[client].npc_viewlist.insert(npc);
		clients[client].myLock.unlock();

		send_put_npc_packet(client, npc);

		wakeup_NPC(npc);
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

void disconnect_client(int id) {
	for (int i = 0; i < MAX_USER; ++i) {
		if (false == clients[i].in_use) continue;
		if (i == id) continue;
		if (0 == clients[i].viewlist.count(id)) continue; // �ٸ� Ŭ���̾�Ʈ�� �丮��Ʈ�� ���� ������ �Ѿ��

		clients[i].myLock.lock();
		clients[i].viewlist.erase(id);
		clients[i].myLock.unlock();

		send_remove_player_packet(i, id);		
	}
	closesocket(clients[id].socket);
	clients[id].in_use = false;
	clients[id].viewlist.clear();
}

void worker_thread() {

	while (true) {
		DWORD io_byte;
		ULONGLONG l_key;
		OVER_EX *over_ex;


		// IOCP�� worker_thread�� recv��, send���� ���� ������ �ʴ´�.. �׷� ��� �����ϳ�..?
		// �츮�� ���� �������� Ȯ�� ����ü�� �������� over�� ���ؼ� �˼��ִ�!!~~!~!!!
		int is_error = GetQueuedCompletionStatus(g_iocp, &io_byte, &l_key, reinterpret_cast<LPOVERLAPPED *>(&over_ex), INFINITE);

		int key = static_cast<int>(l_key);

		// ���� 2���� ���
		// 1. Ŭ�� closesocket���� �ʰ� ������ ���
		if (0 == is_error) {
			int err_no = WSAGetLastError();
			if (64 == err_no) {
				disconnect_client(key);
				continue;
			}
			else err_display("GQCS : ", err_no);
		}

		// 2. Ŭ�� closesocket�ϰ� ������ ���
		if (0 == io_byte) {
			disconnect_client(key);
		}


		if (EVT_RECV == over_ex->event_t) {
			// RECV
//			wcout << "Packet from Client : " << key << endl;

			// ��Ŷ ����
			int rest = io_byte;
			char *ptr = over_ex->messageBuffer;
			char packet_size = 0;

			if (0 < clients[key].prev_size) {
				packet_size = clients[key].packetBuffer[0];
			}

			while (0 < rest) {
				// ��Ŷ����� 0�̵Ǽ� �Ѿ�Դ�? �׷� ����������..
				if (0 == packet_size) {
					packet_size = ptr[0];
				}
				// ��Ŷ�� �ϼ��Ϸ��� ������ �� ����Ʈ �� �޾ƿ;��ϳ�?
				int required = packet_size - clients[key].prev_size;

				// ��Ŷ�� ���� �� ������
				if (required <= rest) {
					// �տ� ���� �����Ͱ� �����Ѵٸ� ����� �Ǵϱ� �߰��ϴ°���
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

void move_npc(int npc_id) {
	int x = npcs[npc_id].x;
	int y = npcs[npc_id].y;

	// npc�� �þ߿� ������ �÷��̾�~~
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

	// �̵� �� npc �þ߿� ������ �÷��̾�~~
	for (int i = 0; i < MAX_USER; ++i) {
		if (false == clients[i].in_use) continue;
		if (false == is_npc_eyesight(i, npc_id)) continue;
		new_vl.insert(i);
	}

	// ���� ���� �÷��̾�, ��� �ִ� �÷��̾� ó��
	for (auto &pl : new_vl) {
		// ���� �̵� �� �丮��Ʈ�� �÷��̾ ���ٸ�
		if (0 == old_vl.count(pl)) {
			// �� �÷��̾� �丮��Ʈ�� ���� �ִ��� Ȯ���ϰ� ������ �־��ֱ�
			if (0 == clients[pl].npc_viewlist.count(npc_id)) {
				clients[pl].myLock.lock();
				clients[pl].npc_viewlist.insert(npc_id);
				clients[pl].myLock.unlock();

				send_put_npc_packet(pl, npc_id);
			}
			else send_npc_pos_packet(pl, npc_id);
		}
		else send_npc_pos_packet(pl, npc_id);
	}

	// ����� �÷��̾� ó��
	for (auto &pl : old_vl) {
		// ���ο� �丮��Ʈ�� ���� �÷��̾��
		if (0 == new_vl.count(pl)) {
			// �� �÷��̾��� �丮��Ʈ�� ���� �ִ��� Ȯ���ϰ� ������ �����ֱ�
			if (0 < clients[pl].npc_viewlist.count(npc_id)) {
				clients[pl].npc_viewlist.erase(npc_id);
				send_remove_npc_packet(pl, npc_id);
			}
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
