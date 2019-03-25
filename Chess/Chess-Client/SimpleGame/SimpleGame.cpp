/*
Copyright 2017 Lee Taek Hee (Korea Polytech University)

This program is free software: you can redistribute it and/or modify
it under the terms of the What The Hell License. Do it plz.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY.
*/
#include "stdafx.h"

#include "Global.h"

// 헤더파일은 64/32비트에 상관없으나 lib(라이브러리)파일은 꼭 확인해주도록하쟈
#include "Dependencies\glew.h"
#include "Dependencies\freeglut.h"
#include "SceneManager.h"

struct SOCKETINFO
{
	WSAOVERLAPPED overlapped;
	WSABUF dataBuffer;
	int receiveBytes;
	int sendBytes;
};

using namespace std;

WSADATA wsa;
SOCKET server_sock;
SOCKADDR_IN server_addr;
char *server_ip = new char[11];


SceneManager *g_scgMgr = NULL;

SOCKETINFO *socketInfo;

int sendRetVal;
int player_num;
Key input_key;
Packet recv_packet;

DWORD g_PrevTime = 0;

//DWORD WINAPI ProcessClient(LPVOID arg);
void err_quit(const char *msg);
void err_display(const char *msg);
int recvn(SOCKET s, char *buf, int len, int flags);
void RecvFromServer(SOCKET server_sock);
void SendToServer(SOCKET server_sock);

void RenderScene();
void Idle();
void MouseInput(int button, int state, int x, int y);
void SpecialKeyUpInput(int key, int x, int y);
void SpecialKeyDownInput(int key, int x, int y);

void Init();


int main(int argc, char **argv)
{
	Init();

	// Initialize GL things
	glutInit(&argc, argv);
	glutInitDisplayMode(GLUT_DEPTH | GLUT_DOUBLE | GLUT_RGBA);
	glutInitWindowPosition(200, 200);
	glutInitWindowSize(800, 800);
	glutCreateWindow("Game Software Engineering KPU");
	glutSetKeyRepeat(GLUT_KEY_REPEAT_OFF);

	glewInit();
	if (glewIsSupported("GL_VERSION_3_0"))
	{
		std::cout << " GLEW Version is 3.0\n ";
	}
	else
	{
		std::cout << "GLEW 3.0 not supported\n ";
	}


	glutDisplayFunc(RenderScene);
	glutIdleFunc(Idle);
	glutMouseFunc(MouseInput);
	glutSpecialFunc(SpecialKeyDownInput);
	glutSpecialUpFunc(SpecialKeyUpInput);

	glutSetKeyRepeat(GLUT_KEY_REPEAT_OFF);

	cout << "서버의 IP를 입력하세요. >> ";
	cin >> server_ip;

	cout << server_ip;

	//CreateThread(NULL, 0, ProcessClient, NULL, 0, NULL);

	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { return 1; }

	server_sock = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (server_sock == INVALID_SOCKET) err_quit("socket()");

	ZeroMemory(&server_addr, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.S_un.S_addr = inet_addr(server_ip);
	server_addr.sin_port = htons(SERVERPORT);

	int retval = 0;
	retval = connect(server_sock, (SOCKADDR *)&server_addr, sizeof(server_addr));
	if (retval == SOCKET_ERROR) {
		err_quit("connect()");
		closesocket(server_sock);
		WSACleanup();
		return 1;
	}
	else { std::cout << "connect() 완료!\n"; }

	// init SceneManager
	g_scgMgr = new SceneManager;

	glutMainLoop();

	delete g_scgMgr;

	closesocket(server_sock);

	WSACleanup();

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

int recvn(SOCKET s, char *buf, int len, int flags)
{
	int received;
	char *ptr = buf;
	int left = len;
	while (left > 0)
	{
		received = recv(s, ptr, left, flags);
		if (received == SOCKET_ERROR)
			return SOCKET_ERROR;
		else if (received == 0)
			break;
		left -= received;
		ptr += received;
	}
	return (len - left);
}

void RecvFromServer(SOCKET s) {
//	if (sendRetVal > 0) {
	int retval;
	char buf[S_PACKET_SIZE + 1];
	ZeroMemory(buf, S_PACKET_SIZE);

	retval = recvn(s, buf, S_PACKET_SIZE, 0);
	if (retval == SOCKET_ERROR) { err_display("recv()"); }

	buf[retval] = '\0';
	memcpy(&recv_packet, buf, S_PACKET_SIZE);

//	cout << " player x" << recv_packet.player_pos[0].x << endl;
//	cout << " player y" << recv_packet.player_pos[0].y << endl;
//	cout << " player " << recv_packet.player_num << endl;


	// cout << "recv from Server " << endl;
}

void SendToServer(SOCKET server_sock) {
	//int retval;
	char buf[C_PACKET_SIZE];

	memcpy(buf, &input_key, C_PACKET_SIZE);

	socketInfo = (struct SOCKETINFO *)malloc(sizeof(struct SOCKETINFO));
	memset((void *)socketInfo, 0x00, sizeof(struct SOCKETINFO));
	socketInfo->dataBuffer.len = C_PACKET_SIZE + 1;
	socketInfo->dataBuffer.buf = buf;

	// cout << "Send2Server " << endl;

	sendRetVal = send(server_sock, buf, sizeof(C_PACKET_SIZE), 0);
	if (sendRetVal == SOCKET_ERROR) { err_display("send()"); }

	RecvFromServer(server_sock);
}

void RenderScene(void) {
	if (g_PrevTime == 0)
	{
		g_PrevTime = GetTickCount();
		return;
	}
	DWORD CurrTime = GetTickCount(); // ms
	DWORD ElapsedTime = CurrTime - g_PrevTime;
	float eTime = (float)ElapsedTime / 1000.0f;
	if (eTime > 0.5f) {
		SendToServer(server_sock);
		g_PrevTime = CurrTime;
	}


	// update
	g_scgMgr->RenderScene();
	glutSwapBuffers();
}

void Idle(void)
{
	RenderScene();
}

void MouseInput(int button, int state, int x, int y)
{
	RenderScene();
}

void SpecialKeyUpInput(int key, int x, int y) {
	switch (key) {
	case GLUT_KEY_UP:
		input_key.keyDown[UP] = false;
		break;
	case GLUT_KEY_DOWN:
		input_key.keyDown[DOWN] = false;
		break;
	case GLUT_KEY_LEFT:
		input_key.keyDown[LEFT] = false;
		break;
	case GLUT_KEY_RIGHT:
		input_key.keyDown[RIGHT] = false;
		break;
	}
	SendToServer(server_sock);
}

void SpecialKeyDownInput(int key, int x, int y) {
	switch (key) {
	case GLUT_KEY_UP:
		input_key.keyDown[UP] = true;
		break;
	case GLUT_KEY_DOWN:
		input_key.keyDown[DOWN] = true;
		break;
	case GLUT_KEY_LEFT:
		input_key.keyDown[LEFT] = true;
		break;
	case GLUT_KEY_RIGHT:
		input_key.keyDown[RIGHT] = true;
		break;
	}
	SendToServer(server_sock);
}

void Init() {

	input_key = { { false, false, false, false } };
}

//DWORD WINAPI ProcessClient(LPVOID arg) {
//}