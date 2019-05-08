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

#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <iostream>
#include <map>

using namespace std;

#include <winsock2.h>
#include "protocol.h"
#include <thread>
#include <vector>
#include <mutex>
//#include <set>
#include <unordered_set>
#include <mutex>


#pragma comment(lib, "Ws2_32.lib")

#define MAX_BUFFER        1024
#define VIEW_RADIUS			3

// 오버랩드 구조체를 확장해서 쓰자
struct OVER_EX{
	WSAOVERLAPPED over;
	WSABUF dataBuffer;
	char messageBuffer[MAX_BUFFER];
	bool is_recv;
};

class SOCKETINFO
{
public:
//	mutex access_lock;
	bool in_use;
	// 클라이언트마다 하나씩 있어야댐.. 하나로 공유하면 덮어써버리는거다... 밑에 4개는 클라마다 꼭 있어야 하는 것.. 必수要소
	unordered_set <int> viewlist;
	mutex myLock;
	OVER_EX over_ex;
	SOCKET socket;
	char packetBuffer[MAX_BUFFER];
	int prev_size;
	char x, y;

	SOCKETINFO() {
		in_use = false;
		over_ex.dataBuffer.len = MAX_BUFFER;
		over_ex.dataBuffer.buf = over_ex.messageBuffer;
		over_ex.is_recv = true;
	}
};
SOCKETINFO clients[MAX_USER];

// NPC 구현 첫번째
// 장점 : 메모리의 낭비가 없다. 직관적이다.
// 단점 : 함수의 중복 구현이 필요하다.
class NPCINFO {
public :
	char x, y;
};
NPCINFO npcs[MAX_NPC];


HANDLE g_iocp;
//SOCKETINFO clients[MAX_USER];


void err_display(const char * msg, int err_no);
int do_accept();
void do_recv(char id);
void send_packet(char client, void *packet);
void send_pos_packet(char client, char pl);
void send_login_ok_packet(char new_id);
void send_remove_player_packet(char clinet, char id);
void send_put_player_packet(char clients, char new_id);
void process_packet(char client, char *packet);
void disconnect_client(char id);
void worker_thread();
bool is_eyesight(char client, char other_client);

bool Is_Near_Object(int a, int b);

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
	wcout << L"에러  [" << err_no << L"] " << lpMsgBuf << endl;
	while (true);
	LocalFree(lpMsgBuf);
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
		clients[new_id].viewlist.clear(); // 뷰리스트 초기화
		ZeroMemory(&clients[new_id].over_ex.over, 
			sizeof(clients[new_id].over_ex.over));
		flags = 0;

		CreateIoCompletionPort(reinterpret_cast<HANDLE>(clientSocket), g_iocp, new_id, 0);

		clients[new_id].in_use = true; // IOCP에 등록 한 다음 true로 바꿔줘야 데이터를 받을 수 있당

		send_login_ok_packet(new_id);
		send_put_player_packet(new_id, new_id);
		for (int i = 0; i < MAX_USER; ++i) {
			if (false == clients[i].in_use) continue;
			if (false == Is_Near_Object(new_id, i)) continue;
			if (i == new_id) continue;

			clients[i].viewlist.insert(new_id);
			send_put_player_packet(i, new_id);
		}

		// 내 위치를 주변 플레이어에게 전송~
		for (int i = 0; i < MAX_USER; ++i) {
			if (false == clients[i].in_use) continue;
			if (i == new_id) continue;
			if (false == Is_Near_Object(i, new_id)) continue;
			clients[new_id].viewlist.insert(i);
			send_put_player_packet(new_id, i);
		}

		//		// 다른 플레이어에게 내 위치를 전송
//		for (int i = 0; i < MAX_USER; ++i) {
//			if (true == clients[i].in_use) {
//				// other player in my eyesight
//				if (is_eyesight(new_id, i) == true) {
//					// other player packet add my id
//					send_put_player_packet(i, new_id);
//
//					if (i == new_id) continue;
//					// other player viewlist insert my id
//					clients[i].myLock.lock();
//					clients[i].viewlist.insert(i);
//					clients[i].myLock.unlock();
//				}
//
//			}
//		}
//
//		// 내 위치를 다른 플레이어에게 전송
//		for (int i = 0; i < MAX_USER; ++i) {
//			if (false == clients[i].in_use) continue;
//			if (i == new_id) continue;
//			
//			// other player in my eyesight
//			if (is_eyesight(i, new_id) == true) {
//				// my packet add other player
//				send_put_player_packet(new_id, i);
//
//				// my view list add other player id
//				clients[new_id].myLock.lock();
//				clients[new_id].viewlist.insert(i);
//				clients[new_id].myLock.unlock();
//			}
//		}

		do_recv(new_id);
	}

	// 6-2. 리슨 소켓종료
	closesocket(listenSocket);

	// Winsock End
	WSACleanup();

	return 0;
}

void do_recv(char id) {
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

void send_packet(char client, void *packet) {
	char *p = reinterpret_cast<char *>(packet);

	OVER_EX *ov = new OVER_EX;
	ov->dataBuffer.len = p[0];
	ov->dataBuffer.buf = ov->messageBuffer;
	ov->is_recv = false;
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
}

void send_pos_packet(char client, char pl) {
	sc_packet_move_player packet;
	packet.id = pl;
	packet.size = sizeof(packet);
	packet.type = SC_MOVE_PLAYER;
	packet.x = clients[pl].x;
	packet.y = clients[pl].y;

	send_packet(client, &packet);
}

void send_login_ok_packet(char new_id) {
	sc_packet_login_ok packet;
	packet.id = new_id;
	packet.size = sizeof(packet);
	packet.type = SC_LOGIN_OK;

	send_packet(new_id, &packet);
}

void send_remove_player_packet(char client, char id) {
	sc_packet_remove_player packet;
	packet.id = id;
	packet.size = sizeof(packet);
	packet.type = SC_REMOVE_PLAYER;

	send_packet(client, &packet);
}

void send_put_player_packet(char client, char new_id) {
	sc_packet_put_player packet;
	packet.id = new_id;
	packet.size = sizeof(packet);
	packet.type = SC_PUT_PLAYER;
	packet.x = clients[new_id].x;
	packet.y = clients[new_id].y;

	send_packet(client, &packet);
}

void process_packet(char client, char * packet) {
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
		wcout << L"정의되지 않은 패킷 도착 오류!!\n";
		while (true);
	}

	clients[client].x = x;
	clients[client].y = y;

	unordered_set <int> new_vl;
	for (int i = 0; i < MAX_USER; ++i) {
		if (i == client) continue;
		if (false == Is_Near_Object(i, client)) continue;
		new_vl.insert(i); // 이동 후에 보인 애들이 여기에 들어가있겠지.. 이걸로 비교하면 된다!
	}

	send_pos_packet(client, client);

	// 3가지 Case가 있다
	// Case 1. old_vl 에도 있고 new_vl에도 있는 플레이어 -> Send packet~
	for (auto player : old_vl) {
		if (0 == new_vl.count(player)) continue; // 새 뷰리스트에 상대방이 없다~
		if (0 == clients[player].viewlist.count(client)) { // 상대방 뷰리스트에 나도 있으면 걍 보내기~
			send_pos_packet(player, client);
		}
		else { // 나는 얘가 있는데 상대방 뷰리스트에 내가 없어.. 그럼 상대방 뷰리스트에 나를 추가하고 보내기
			clients[player].viewlist.insert(client);
			send_put_player_packet(player, client);
		}
	}

	// Case 2. old_vl 에 없음, new_vl 에는 있음 -> 내 시야에 들어온거임~
	for (auto player : new_vl) {
		if (0 < old_vl.count(player)) continue; // 옛날 뷰리스트에 상대방이 있으면 통과~

		// 없으면 내 뷰리스트에 추가해주자
		clients[client].viewlist.insert(player);
		send_put_player_packet(client, player);

		if (0 == clients[player].viewlist.count(client)) {
			clients[player].viewlist.insert(client);
			send_put_player_packet(player, client);
		}
		else {
			send_pos_packet(player, client);
		}
	}

	// Case 3. old_vl에 있었는데 new_vl에는 없는 경우 -> 내 시야에서 사라진 경우
	for (auto player : old_vl) {
		if (0 < new_vl.count(player)) continue;

		clients[client].viewlist.erase(player);
		send_remove_player_packet(client, player);

		if (0 < clients[player].viewlist.count(client)) {
			clients[player].viewlist.erase(client);
			send_remove_player_packet(player, client);
		}
	}

//	unordered_set<int> nearlist;
//	nearlist.clear();
//
//	// add other player id in nearlist!
//	for (int i = 0; i < MAX_USER; ++i) {
//		if (false == clients[i].in_use) continue;
//		if (i == client) continue;
//
//		// other player in my eyesight
//		if (is_eyesight(i, client) == true) {
//			nearlist.insert(i);
//		}
//
//	}
//	send_pos_packet(client, client);
//
//	if (!nearlist.empty()) {
//		// near -> view
//		for (auto &id : nearlist) {
//			cout << "in nearlist id : " << id << endl;
//
//			auto iter = clients[client].viewlist.find(id);
//
//			// no my viewlist
//			if (iter == clients[client].viewlist.end()) {
//				clients[client].myLock.lock();
//				clients[client].viewlist.insert(id);
//				clients[client].myLock.unlock();
//
//				send_put_player_packet(client, id);
//			}
//
//			auto other_iter = clients[id].viewlist.find(client);
//
//			// no me other player viewlist
//			if (other_iter == clients[id].viewlist.end()) {
//				clients[id].myLock.lock();
//				clients[id].viewlist.insert(client);
//				clients[id].myLock.unlock();
//
//				send_put_player_packet(id, client);
//			}
//
//			// yes me other player viewlist
//			else {
//				send_pos_packet(id, client);
//			}
//		}
//	}
//
//
//	clients[client].myLock.lock();
//	unordered_set<int> temp_viewlist = clients[client].viewlist;
//	clients[client].myLock.unlock();
//
//	// view -> near
//	if (!temp_viewlist.empty()) {
////		cout << "here pung2" << endl;
//		for (auto &id : temp_viewlist) {
//			if (id == client) continue;
//
//			auto iter = nearlist.find(id);
//
//			// no my nearlist
//			if (iter == nearlist.end()) {
//				clients[client].myLock.lock();
//				clients[client].viewlist.erase(id);
//				clients[client].myLock.unlock();
//
//				send_remove_player_packet(client, id);
//				cout << "remove id" << id << endl;
//
//			}
//			cout << "near id" << id << endl;
//		}
//	}
//	


}

void disconnect_client(char id) {
	for (int i = 0; i < MAX_USER; ++i) {
		if (false == clients[i].in_use) continue;
		if (i == id) continue;
		if (0 == clients[i].viewlist.count(id)) continue;

		clients[i].viewlist.erase(id);
		send_remove_player_packet(i, id);

		//clients[i].myLock.lock();
		//unordered_set<int> temp_viewlist = clients[i].viewlist;
		//clients[i].myLock.unlock();
		//
		//auto iter = temp_viewlist.find(id);
		//if (iter != temp_viewlist.end()) {
		//	clients[i].myLock.lock();
		//	clients[i].viewlist.erase(id);
		//	clients[i].myLock.unlock();
		//
		//	send_remove_player_packet(i, id);
		//}
		//
		
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

		// IOCP는 worker_thread가 recv용, send용이 따로 나뉘지 않는다.. 그럼 어떻게 구분하냐..?
		// 우리가 만든 오버랩드 확장 구조체의 포인터인 over를 통해서 알수있다!!~~!~!!!
		int is_error = GetQueuedCompletionStatus(g_iocp, &io_byte, &l_key, reinterpret_cast<LPWSAOVERLAPPED *>(&over_ex), INFINITE);

		char key = static_cast<char>(l_key);

		// 에러 2가지 경우
		// 1. 클라가 closesocket하지 않고 종료한 경우
		if (0 == is_error) {
			int err_no = WSAGetLastError();
			if (64 == err_no) disconnect_client(key);
			else err_display("GQCS : ", err_no);
		}

		// 2. 클라가 closesocket하고 종료한 경우
		if (0 == io_byte) {
			disconnect_client(key);
		}

		if (true == over_ex->is_recv) {
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
		else {
			if(false == over_ex->is_recv)
				delete over_ex;
		}
	}
}

bool is_eyesight(char client, char other_client) {
	int x = clients[client].x - clients[other_client].x;
	int y = clients[client].y - clients[other_client].y;

	int distance = (x * x) + (y * y);
	int eyesight = 7;

	if (distance < (eyesight*eyesight)) return true;
	else return false;
}

bool Is_Near_Object(int a, int b) {
	if (VIEW_RADIUS < abs(clients[a].x - clients[b].x))
		return true;
	if (VIEW_RADIUS < abs(clients[a].y - clients[b].y))
		return true;
	else
		return false;
}