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
#include <map>

using namespace std;

#include <winsock2.h>
#include "protocol.h"
#include <thread>
#include <vector>
#include <mutex>
#include <unordered_set>
#include <mutex>


#pragma comment(lib, "Ws2_32.lib")

#define MAX_BUFFER        1024
#define VIEW_RADIUS			15

// �������� ����ü�� Ȯ���ؼ� ����
struct OVER_EX{
	WSAOVERLAPPED over;
	WSABUF dataBuffer;
	char messageBuffer[MAX_BUFFER];
	bool is_recv;
};

//class SOCKETINFO
//{
//public:
////	mutex access_lock;
//	bool in_use;
//	// Ŭ���̾�Ʈ���� �ϳ��� �־�ߴ�.. �ϳ��� �����ϸ� ���������°Ŵ�... �ؿ� 4���� Ŭ�󸶴� �� �־�� �ϴ� ��.. ����驼�
//	unordered_set <int> viewlist;
//	mutex myLock;
//	OVER_EX over_ex;
//	SOCKET socket;
//	char packetBuffer[MAX_BUFFER];
//	int prev_size;
//	int x, y;
//
//	SOCKETINFO() {
//		in_use = false;
//		over_ex.dataBuffer.len = MAX_BUFFER;
//		over_ex.dataBuffer.buf = over_ex.messageBuffer;
//		over_ex.is_recv = true;
//	}
//};
//SOCKETINFO clients[MAX_USER];
//
//// NPC ���� ù��°
//class NPCINFO {
//public :
//	int x, y;
//};
//NPCINFO npcs[MAX_NPC];
// ���� : �޸��� ���� ����. �������̴�.
// ���� : �Լ��� �ߺ� ������ �ʿ��ϴ�.
// // bool Is_Near_Object(int a, int b); �� �Լ� �ϳ���
// // bool Is_Near_Player_Player, Is_Near_Player_Npc, Is_Near_Npc_Npc;
 
 
// NPC ���� �ι�°
 class NPCINFO {
 public :
 	char x, y;
 };
 
 class SOCKETINFO : public NPCINFO {
 	bool in_use;
 	mutex myLock;
 	OVER_EX over_ex;
 	SOCKET socket;
 	char packetBuffer[MAX_BUFFER];
 	int prev_size;
 	unordered_set <int> viewlist;
 	
 	SOCKETINFO() {
 		in_use = false;
 		over_ex.dataBuffer.len = MAX_BUFFER;
 		over_ex.dataBuffer.buf = over_ex.messageBuffer;
 		over_ex.is_recv = true;
 	}
 };
 
 NPCINFO *obejcts[MAX_USER + MAX_NPC];
 // ���� : �޸��� ���� ����. �Լ��� �ߺ� ������ �ʿ� ����.
 // ���� : �������� ���. ��������
 // Ư¡ : ID ����� �ϰų�, object_type ����� �ʿ�. (�߰����� ���⵵�� �þ)
// 
// 
// 
// NPC ���� �ǽ� �ܼ� ����
// ID : 0 ~ 9 �÷��̾�
// ID : 10 ~ 10 + NUM_NPC - 1 ������ NPC
// SOCKETINFO clients[MAX_USER + MAX_NPC];
// ���� : ������ ���X
// ���� : ���� �޸� ����

HANDLE g_iocp;
//SOCKETINFO clients[MAX_USER];


void err_display(const char * msg, int err_no);
int do_accept();
void do_recv(int id);
void send_packet(int client, void *packet);
void send_pos_packet(int client, int pl);
void send_login_ok_packet(int new_id);
void send_remove_player_packet(int clinet, int id);
void send_put_player_packet(int clients, int new_id);
void process_packet(int client, char *packet);
void disconnect_client(int id);
void worker_thread();
bool is_eyesight(int client, int other_client);

//bool Is_Near_Object(int a, int b);

int main() {
	vector <thread> worker_threads;

	wcout.imbue(locale("korean"));

	g_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);
	for (int i = 0; i < 4; ++i) {
		worker_threads.emplace_back(thread{ worker_thread });
	}

	thread accept_thread{ do_accept };

	accept_thread.join();

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
		clients[new_id].x = clients[new_id].y = 50;
		clients[new_id].viewlist.clear(); // �丮��Ʈ �ʱ�ȭ
		ZeroMemory(&clients[new_id].over_ex.over, 
			sizeof(clients[new_id].over_ex.over));
		flags = 0;

		CreateIoCompletionPort(reinterpret_cast<HANDLE>(clientSocket), g_iocp, new_id, 0);

		clients[new_id].in_use = true; // IOCP�� ��� �� ���� true�� �ٲ���� �����͸� ���� �� �ִ�

		send_login_ok_packet(new_id);
		send_put_player_packet(new_id, new_id); // ������ �� ��ġ ������

		//for (int i = 0; i < MAX_USER; ++i) {
		//	if (false == clients[i].in_use) continue;
		//	if (false == is_eyesight(new_id, i)) continue;
		//	if (i == new_id) continue;

		//	clients[i].viewlist.insert(new_id);
		//	send_put_player_packet(i, new_id);
		//}

		// �ٸ� �÷��̾�� �� ��ġ�� ����
		for (int i = 0; i < MAX_USER; ++i) {
			if (false == clients[i].in_use) continue;
			if (false == is_eyesight(new_id, i)) continue;
			if (i == new_id) continue;

			clients[i].myLock.lock();
			clients[i].viewlist.insert(new_id);
			clients[i].myLock.unlock();
			
			send_put_player_packet(i, new_id);
		}


		//// �� ��ġ�� �ֺ� �÷��̾�� ����~
		//for (int i = 0; i < MAX_USER; ++i) {
		//	if (false == clients[i].in_use) continue;
		//	if (i == new_id) continue;
		//	if (false == is_eyesight(i, new_id)) continue;
		//	clients[new_id].viewlist.insert(i);
		//	send_put_player_packet(new_id, i);
		//}

		// �� ��ġ�� �ٸ� �÷��̾�� ����
		for (int i = 0; i < MAX_USER; ++i) {
			if (false == clients[i].in_use) continue;
			if (i == new_id) continue;
			if (false == is_eyesight(i, new_id)) continue;

			clients[new_id].myLock.lock();
			clients[new_id].viewlist.insert(i);
			clients[new_id].myLock.unlock();

			send_put_player_packet(new_id, i);
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
	ov->is_recv = false;
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
}

void send_pos_packet(int client, int pl) {
	sc_packet_move_player packet;
	packet.id = pl;
	packet.size = sizeof(packet);
	packet.type = SC_MOVE_PLAYER;
	packet.x = clients[pl].x;
	packet.y = clients[pl].y;

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

void send_put_player_packet(int client, int new_id) {
	sc_packet_put_player packet;
	packet.id = new_id;
	packet.size = sizeof(packet);
	packet.type = SC_PUT_PLAYER;
	packet.x = clients[new_id].x;
	packet.y = clients[new_id].y;

	send_packet(client, &packet);
}

void process_packet(int client, char * packet) {
	cs_packet_up *p = reinterpret_cast<cs_packet_up *>(packet);

	int x = clients[client].x;
	int y = clients[client].y;

	auto old_vl = clients[client].viewlist;

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
		if (i == client) continue; // ���� �Ѿ��
		if (false == is_eyesight(i, client)) continue; // �Ⱥ��̸� �߰�����
		new_vl.insert(i); // �̵� �Ŀ� ���� �ֵ��� ���⿡ ���ְ���.. �̰ɷ� ���ϸ� �ȴ�!
	}

	send_pos_packet(client, client);

	// 3���� Case�� �ִ�
	// Case 1. old_vl ���� �ְ� new_vl���� �ִ� �÷��̾� -> Send packet~
	for (auto player : old_vl) {
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
	for (auto player : new_vl) {
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
	for (auto player : old_vl) {
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
		int is_error = GetQueuedCompletionStatus(g_iocp, &io_byte, &l_key, reinterpret_cast<LPWSAOVERLAPPED *>(&over_ex), INFINITE);

		char key = static_cast<char>(l_key);

		// ���� 2���� ���
		// 1. Ŭ�� closesocket���� �ʰ� ������ ���
		if (0 == is_error) {
			int err_no = WSAGetLastError();
			if (64 == err_no) disconnect_client(key);
			else err_display("GQCS : ", err_no);
		}

		// 2. Ŭ�� closesocket�ϰ� ������ ���
		if (0 == io_byte) {
			disconnect_client(key);
		}

		if (true == over_ex->is_recv) {
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
		else {
			if(false == over_ex->is_recv)
				delete over_ex;
		}
	}
}

bool is_eyesight(int client, int other_client) {
	int x = clients[client].x - clients[other_client].x;
	int y = clients[client].y - clients[other_client].y;

	int distance = (x * x) + (y * y);
	int eyesight = 7;

	if (distance < (VIEW_RADIUS * VIEW_RADIUS)) return true;
	else return false;
}
