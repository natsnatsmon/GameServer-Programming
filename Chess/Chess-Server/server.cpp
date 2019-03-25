#define _WINSOCK_DEPRECATED_NO_WARNINGS
#pragma comment(lib, "ws2_32")

#include <winsock2.h>
#include <iostream>
#include <map>
#include <stdio.h>
#include "global.h"

using namespace std;

map <SOCKET, SOCKETINFO> clients;

void CALLBACK recv_callback(DWORD Error, DWORD dataBytes, LPWSAOVERLAPPED overlapped, DWORD lnFlags);
void CALLBACK send_callback(DWORD Error, DWORD dataBytes, LPWSAOVERLAPPED overlapped, DWORD lnFlags);

SOCKET listen_sock;
SOCKET client_sock;
Packet send_packet;
Pos player[MAX_PLAYER];
Key input_key;
short player_num = 0;


void err_quit(const char *msg);
void err_display(const char *msg);
int recvn(SOCKET s, char *buf, int len, int flags);

void Init();
int RecvFromClient(SOCKET client_sock);
void SendToClient();
void UpdatePosition(int playeId);

int main(int argc, char *argv[]) {
	int retval;
	Init();

	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { return 1; }

	// 소켓 생성
	listen_sock = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (listen_sock == INVALID_SOCKET) { err_quit("socket()"); }

	// 소켓 옵션 설정
	BOOL NoDelay = TRUE;
	retval = setsockopt(listen_sock, IPPROTO_TCP, TCP_NODELAY, (const char FAR*)&NoDelay, sizeof(NoDelay));
	if (retval == SOCKET_ERROR) { err_quit("bind()"); }

	// bind
	SOCKADDR_IN server_addr;
	ZeroMemory(&server_addr, sizeof(SOCKADDR_IN));
	server_addr.sin_family = PF_INET;
	server_addr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
	server_addr.sin_port = ntohs(SERVERPORT);
	retval = bind(listen_sock, (SOCKADDR*)&server_addr, sizeof(server_addr));
	if (retval == SOCKET_ERROR) { err_quit("bind()"); }

	// listen
	retval = listen(listen_sock, SOMAXCONN);
	if (retval == SOCKET_ERROR) { err_quit("listen()");	}

	printf("[ CHESS 서버가 열렸습니다. ]\n");

	SOCKADDR_IN client_addr;
	int addrLen = sizeof(SOCKADDR_IN);
	memset(&client_addr, 0, addrLen);
	DWORD flags;

	while (1) {
		client_sock = accept(listen_sock, (SOCKADDR *)&client_addr, &addrLen);

		if (client_sock == INVALID_SOCKET) {
			err_display("accept()");
			break;
		}

		player_num++;

		clients[client_sock] = SOCKETINFO{};
		memset(&clients[client_sock], 0x00, sizeof(struct SOCKETINFO));
		clients[client_sock].socket = client_sock;
		clients[client_sock].dataBuffer.len = MAX_BUFFER;
		clients[client_sock].dataBuffer.buf = clients[client_sock].buf;
		clients[client_sock].playerId = player_num - 1;
		send_packet.player_num = player_num;
		flags = 0;

		clients[client_sock].overlapped.hEvent = (HANDLE)clients[client_sock].socket;

		if (WSARecv(clients[client_sock].socket, &clients[client_sock].dataBuffer, 1, NULL,
			&flags, &(clients[client_sock].overlapped), recv_callback)) {
			if (WSAGetLastError() != WSA_IO_PENDING) {
				printf("Error - IO pending Failure\n");
				return 1;
			}
		}
		else {
			printf("Non Overlapped Recv return.\n");
			return 1;
		}

		//printf("\n[서버] 클라이언트 접속, IP : %s, 포트번호 : %d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
	}
	closesocket(listen_sock);

	WSACleanup();

	printf("[서버가 닫혔습니다]\n");

	return 0;
}

void err_quit(const char *msg)
{
	LPVOID lpMsgBuf;
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, WSAGetLastError(),
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf, 0, NULL);
	MessageBox(NULL, (LPCTSTR)lpMsgBuf, msg, MB_ICONERROR);
	LocalFree(lpMsgBuf);
	exit(1);
}
void err_display(const char *msg)
{
	LPVOID lpMsgBuf;
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, WSAGetLastError(),
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf, 0, NULL);
	printf("[%s] %s", msg, (char *)lpMsgBuf);
	LocalFree(lpMsgBuf);
}

int recvn(SOCKET s, char *buf, int len, int flags) {
	int received;
	char *ptr = buf;
	int left = len;

	while (left > 0) {
		received = recv(s, ptr, left, flags);
		if (received == SOCKET_ERROR) {	return SOCKET_ERROR; }
		else if (received == 0) { break; }

		left -= received;
		ptr += received;
	}

	return (len - left);
}

void Init() {
	for (int i = 0; i < MAX_PLAYER; ++i) {
		if (i < 5) {
			player[i].x = i * 100.f - 250.f;
			player[i].y = 150.f;
		}
		else {

			player[i].x = i * 100.f - 550.f;
			player[i].y = -150.f;
		}
		send_packet.player_pos[i].x = 0.f;
		send_packet.player_pos[i].y = 0.f;
	}
	input_key = { { false, false, false, false } };

	send_packet.player_num = 0;
}

void CALLBACK recv_callback(DWORD Error, DWORD dataBytes, LPWSAOVERLAPPED overlapped, DWORD lnFlags) {

	SOCKET client_s = reinterpret_cast<int>(overlapped->hEvent);

	DWORD sendBytes = 0;
	DWORD receiveBytes = 0;
	DWORD flags = 0;

	int id = clients[client_s].playerId;

	// 클라가 closesocket 했을 경우
	if (dataBytes == 0) {
		closesocket(clients[client_s].socket);
		clients.erase(client_s);
		//send_packet.player_num -= 1;
		return;
	}

	// 좌표 업데이트
	memcpy(&input_key, clients[client_s].buf, C_PACKET_SIZE);
	//cout << "PLAYER " << clients[client_s].playerId << endl;
	//cout << "UP " << input_key.keyDown[UP] << endl;
	//cout << "DOWN " << input_key.keyDown[DOWN] << endl;
	//cout << "LEFT " << input_key.keyDown[LEFT] << endl;
	//cout << "RIGHT " << input_key.keyDown[RIGHT] << endl;

	UpdatePosition(clients[client_s].playerId);

	printf("Recv Packet\n");

	// 패킷 업데이트
	memcpy(&send_packet.player_pos, &player, 80);
	send_packet.player_num = player_num;

	// 좌표 확인
	cout << "SEND POS x, y" << endl;
//	cout << send_packet.player_pos[0].x << endl;
//	cout << send_packet.player_pos[0].y << endl;

	cout << "player_num " << player_num << endl;

//	for (int i = 0; i < MAX_PLAYER; ++i) {
//		cout << "SEND POS x, y";
//		cout << send_packet.player_pos[i].x << endl;
//		cout << send_packet.player_pos[i].y << endl;
//		cout << send_packet.player_num << endl;
//	}


	clients[client_s].dataBuffer.len = S_PACKET_SIZE;
	cout << "recv_callback dataBytes - " << dataBytes << endl;
	memcpy(clients[client_s].dataBuffer.buf, &send_packet, S_PACKET_SIZE);

	// 리셋하는데
	memset(&(clients[client_s].overlapped), 0x00, sizeof(WSAOVERLAPPED));
	clients[client_s].overlapped.hEvent = (HANDLE)client_s;


	// 연결 소켓, WSABUF 구조체 배열의 포인터, WSABUF구조체 개수, 함수의 호출로 전송된 데이터 바이트의 크기, 플래그, 
	// 오버랩드 구조체의 포인터, 데이터 전송이 완료되었을 때 호출할 완료 루틴의 포인터
	if (WSASend(client_s, &(clients[client_s].dataBuffer), 1, &dataBytes, 0,
		&(clients[client_s].overlapped), send_callback) == SOCKET_ERROR) {
		if (WSAGetLastError() != WSA_IO_PENDING) {
			printf("Error - Fail WSASend(error_code : %d)\n", WSAGetLastError());
		}
	}
}


void CALLBACK send_callback(DWORD Error, DWORD dataBytes, LPWSAOVERLAPPED overlapped, DWORD lnFlags) {
	SOCKET client_s = reinterpret_cast<int>(overlapped->hEvent);

	DWORD flags = 0;

	// 클라가 closesocket 했을 경우

	if (dataBytes == 0) {
		closesocket(clients[client_s].socket);
		clients.erase(client_s);
		return;
	}

	{
		cout << "send_callback dataBytes - " << dataBytes << endl;

		memset(&(clients[client_s].overlapped), 0x00, sizeof(WSAOVERLAPPED));
		clients[client_s].overlapped.hEvent = (HANDLE)client_s;
		
		printf("Send Packet\n");
		// 연결 소켓, WSABUF 포인터, WSABUF 구조체 개수, 데이터 입력이 완료된 경우 읽은 데이터의 바이트 크기,
		// 플래그, 오버랩 구조체 포인터, 데이터 입력이 완료되었을 때 호출할 루틴의 포인터
		if (WSARecv(client_s, &clients[client_s].dataBuffer, 1, NULL,
			&flags, &(clients[client_s].overlapped), recv_callback) == SOCKET_ERROR)
		{
			if (WSAGetLastError() != WSA_IO_PENDING)
			{
				printf("Error - Fail WSARecv(error_code : %d)\n", WSAGetLastError());
			}
		}
	}


}

int RecvFromClient(SOCKET client_sock) {
	int retval;

	SOCKET sock = client_sock;
	SOCKADDR_IN client_addr;
	int addrLen;

	addrLen = sizeof(client_addr);
	getpeername(sock, (SOCKADDR *)&client_addr, &addrLen);

	char buf[C_PACKET_SIZE + 1];
	ZeroMemory(buf, C_PACKET_SIZE);

	retval = recv(client_sock, buf, C_PACKET_SIZE, 0);
	if (retval == SOCKET_ERROR) { return retval; }
	
	buf[retval] = '\0';

	memcpy(&input_key, buf, C_PACKET_SIZE);

	return 0;
}

void SendToClient() {
	int retval;

	char buf[S_PACKET_SIZE];

	ZeroMemory(buf, S_PACKET_SIZE);

	memcpy(buf, &player, S_PACKET_SIZE);

	retval = send(client_sock, buf, S_PACKET_SIZE, 0);
	if (retval == SOCKET_ERROR) {
		closesocket(client_sock);
		client_sock = 0;
	}

	//printf("Send : %f, %f\n", player.x, player.y);

}

void UpdatePosition(int playerId) {
	float amount = 100.f;

//	cout << "BEFORE" << endl;
//	cout << "X " << player[playerId].x << endl;
//	cout << "Y " << player[playerId].y << endl;


	//send_packet.player_pos[0].x
	if (input_key.keyDown[UP])   { player[playerId].y += amount; }
	if (input_key.keyDown[DOWN]) { player[playerId].y -= amount; }
	if (input_key.keyDown[RIGHT]){ player[playerId].x += amount; }
	if (input_key.keyDown[LEFT]) { player[playerId].x -= amount; }

//	cout << "AFTER" << endl;
//	cout << "X " << player[playerId].x << endl;
//	cout << "Y " << player[playerId].y << endl;
}


// 순서 : WSARECV -> 버퍼에 정보 받아옴 -> recv_callback -> 버퍼에 패킷 복사 -> WSASEND -> 클라 버퍼에 패킷 보냄 -> send_callback