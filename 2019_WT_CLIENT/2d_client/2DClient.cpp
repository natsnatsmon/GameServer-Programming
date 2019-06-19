// PROG14_1_16b.CPP - DirectInput keyboard demo

// INCLUDES ///////////////////////////////////////////////

#define WIN32_LEAN_AND_MEAN  
#define INITGUID
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS

#include <WinSock2.h>
#include <windows.h>   // include important windows stuff
#include <windowsx.h>
#include <stdio.h>
#include "resource.h"

#include <d3d9.h>     // directX includes
#include "d3dx9tex.h"     // directX includes
#include "gpdumb1.h"
#include "2019_텀프_protocol.h"

#pragma comment (lib, "ws2_32.lib")

// DEFINES ////////////////////////////////////////////////

#define MAX(a,b)	((a)>(b))?(a):(b)
#define	MIN(a,b)	((a)<(b))?(a):(b)

// defines for windows 
#define WINDOW_CLASS_NAME L"WINXCLASS"  // class name

#define WINDOW_WIDTH    720   // size of window
#define WINDOW_HEIGHT   840

#define	BUF_SIZE				1024
#define	WM_SOCKET				WM_USER + 1

// PROTOTYPES /////////////////////////////////////////////

// game console
int Game_Init(void *parms = NULL);
int Game_Shutdown(void *parms = NULL);
int Game_Main(void *parms = NULL);

// GLOBALS ////////////////////////////////////////////////

HWND hIdInputBox, hSendButton, hKindRadioButton;
HWND main_window_handle = NULL; // save the window handle
HINSTANCE main_instance = NULL; // save the instance
char buffer[80];                // used to print text

								// demo globals
BOB			player;				// 플레이어 Unit
BOB			tmp_fairy_player, tmp_devil_player, tmp_angel_player;
BOB			npc[NUM_NPC];      // NPC Unit
BOB         skelaton[MAX_USER];     // the other player skelaton

BITMAP_IMAGE reactor;      // the background   

BITMAP_IMAGE grass_tile;
BITMAP_IMAGE sand_tile;

#define TILE_WIDTH 32
#define UNIT_TEXTURE  0

char	cl_id[20];
SOCKET  g_mysocket;
WSABUF	send_wsabuf;
char 	send_buffer[BUF_SIZE];
WSABUF	recv_wsabuf;
char	recv_buffer[BUF_SIZE];
char	packet_buffer[BUF_SIZE];
DWORD		in_packet_size = 0;
int		saved_packet_size = 0;

int		g_left_x = 0;
int     g_top_y = 0;

// my info
int		g_myid, g_myexp, g_mygold;
char	g_mytype, g_mykind;
unsigned short g_mylevel, g_myhp, g_myattack;
bool	is_login_ok = false;


// FUNCTIONS //////////////////////////////////////////////
void ProcessPacket(char *ptr)
{
	static bool first_time = true;
	switch (ptr[1])
	{
	case SC_LOGIN_OK:
	{
		sc_packet_login_ok *packet = 
			reinterpret_cast<sc_packet_login_ok *>(ptr);
		g_myid = packet->id;
		g_mykind = packet->kind;
		g_myhp = packet->HP;
		g_mylevel = packet->LEVEL;
		g_myattack = packet->ATTACK;
		g_myexp = packet->EXP;
		g_mygold = packet->GOLD;
		is_login_ok = true;

		Load_Texture(L"TA_player.PNG", UNIT_TEXTURE, 128, 32);

		if (!Create_BOB32(&player, 0, 0, 32, 32, 1, BOB_ATTR_SINGLE_FRAME)) return;
		Load_Frame_BOB32(&player, UNIT_TEXTURE, 0, (int)g_mykind+1, 0, BITMAP_EXTRACT_MODE_CELL);

		player.x = packet->x;
		player.y = packet->y;
	}
	break;
	case SC_LOGIN_FAIL:
	{
		sc_packet_login_fail *packet =
			reinterpret_cast<sc_packet_login_fail *>(ptr);
		is_login_ok = false;

		Game_Shutdown();
	}
	break;
	case SC_POSITION:
	{
		sc_packet_position *my_packet = reinterpret_cast<sc_packet_position *>(ptr);
		int id = my_packet->id;
		if (my_packet->obj_class == PLAYER) {
			if (id == g_myid) {
				g_left_x = my_packet->x - 10;
				g_top_y = my_packet->y - 10;
				player.x = my_packet->x;
				player.y = my_packet->y;
			}
			else if (id < MAX_USER) {
				skelaton[id].x = my_packet->x;
				skelaton[id].y = my_packet->y;
			}
		}

		else if (my_packet->obj_class == NPC) {
			npc[id].x = my_packet->x;
			npc[id].y = my_packet->y;
		}

		break;
	}
	case SC_ADD_OBJECT:
	{
		sc_packet_add_object *my_packet = reinterpret_cast<sc_packet_add_object *>(ptr);
		int id = my_packet->id;

		if (my_packet->obj_class == PLAYER) {
			if (id == g_myid) {
				g_mykind = my_packet->kind;
				player.x = my_packet->x;
				player.y = my_packet->y;
				g_myhp = my_packet->HP;
				g_mylevel = my_packet->LEVEL;
				g_myexp = my_packet->EXP;
				player.attr |= BOB_ATTR_VISIBLE;
			}
			else if (id < MAX_USER) {
				skelaton[id].x = my_packet->x;
				skelaton[id].y = my_packet->y;
				skelaton[id].attr |= BOB_ATTR_VISIBLE;
			}
		}
		else if(my_packet->obj_class == NPC) {
			npc[id].kind = my_packet->kind;
			npc[id].x = my_packet->x;
			npc[id].y = my_packet->y;
			npc[id].hp = my_packet->HP;
			npc[id].level = my_packet->LEVEL;
			npc[id].exp = my_packet->EXP;
			npc[id].attr |= BOB_ATTR_VISIBLE;
		}
	}
	break;
	case SC_REMOVE_OBJECT:
	{
		sc_packet_remove_object *my_packet = reinterpret_cast<sc_packet_remove_object *>(ptr);
		int other_id = my_packet->id;

		if (my_packet->obj_class == PLAYER) {
			if (other_id == g_myid) {
				player.attr &= ~BOB_ATTR_VISIBLE;
			}
			else if (other_id < MAX_USER) {
				skelaton[other_id].attr &= ~BOB_ATTR_VISIBLE;
			}
		}
		else if(my_packet->obj_class == NPC) {
			npc[other_id].attr &= ~BOB_ATTR_VISIBLE;
		}
	}
		break;
	case SC_CHAT:
	{
		sc_packet_chat *my_packet = reinterpret_cast<sc_packet_chat *>(ptr);
		int other_id = my_packet->id;
		int obj_type = my_packet->obj_class;

		if (obj_type == PLAYER && other_id == g_myid) {
			wcsncpy_s(player.message, my_packet->message, 256);
			player.message_time = GetTickCount();
		}
		else if (obj_type == PLAYER && other_id < MAX_USER) {
			wcsncpy_s(skelaton[other_id].message, my_packet->message, 256);
			skelaton[other_id].message_time = GetTickCount();
		}
		else {
			wcsncpy(npc[other_id].message, my_packet->message, 256);
			npc[other_id].message_time = GetTickCount();
		}

	}
		break;

	case SC_STAT_CHANGE:
	{
		sc_packet_stat_change *my_packet = reinterpret_cast<sc_packet_stat_change *>(ptr);
		int other_id = my_packet->id;
		int obj_type = my_packet->obj_class;

		if (obj_type == PLAYER && other_id == g_myid) {
			g_myhp = my_packet->HP;
			g_mygold = my_packet->GOLD;
			g_mylevel = my_packet->LEVEL;
			g_myexp = my_packet->EXP;
		}
		else if (obj_type == PLAYER && other_id < MAX_USER) {
		}
	}
	break;

	//case SC_NPC_CHAT:
	//{
	//	sc_packet_chat *my_packet = reinterpret_cast<sc_packet_chat *>(ptr);
	//	int other_id = my_packet->id;
	//	wcsncpy(npc[other_id].message, my_packet->message, 256);
	//	npc[other_id].message_time = GetTickCount();
	//	break;

	//}
	default:
		printf("Unknown PACKET type [%d]\n", ptr[1]);
	}
}

void ReadPacket(SOCKET sock)
{
	DWORD iobyte, ioflag = 0;

	int ret = WSARecv(sock, &recv_wsabuf, 1, &iobyte, &ioflag, NULL, NULL);
	if (ret) {
		int err_code = WSAGetLastError();
		printf("Recv Error [%d]\n", err_code);
	}

	BYTE *ptr = reinterpret_cast<BYTE *>(recv_buffer);

	while (0 != iobyte) {
		if (0 == in_packet_size) in_packet_size = ptr[0];
		if (iobyte + saved_packet_size >= in_packet_size) {
			memcpy(packet_buffer + saved_packet_size, ptr, in_packet_size - saved_packet_size);
			ProcessPacket(packet_buffer);
			ptr += in_packet_size - saved_packet_size;
			iobyte -= in_packet_size - saved_packet_size;
			in_packet_size = 0;
			saved_packet_size = 0;
		}
		else {
			memcpy(packet_buffer + saved_packet_size, ptr, iobyte);
			saved_packet_size += iobyte;
			iobyte = 0;
		}
	}
}

void clienterror()
{
	exit(-1);
}

BOOL CALLBACK DlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	static wchar_t buf[21];
	int buf_len = 0;
	cs_packet_login *my_packet = reinterpret_cast<cs_packet_login *>(send_buffer);
	int ret = 0;
	int select_kind = 0;

	switch (uMsg) {
	case WM_INITDIALOG:
		hIdInputBox = GetDlgItem(hDlg, IDC_EDIT1);
		hSendButton = GetDlgItem(hDlg, IDOK);
		//hKindRadioButton = GetDlgItem(hDlg, IDC_RADIO1);
		SendMessage(hIdInputBox, EM_SETLIMITTEXT, 20, 0);
		return TRUE;

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDOK:
			if(IsDlgButtonChecked(hDlg, IDC_RADIO1)) select_kind = 0;
			else if (IsDlgButtonChecked(hDlg, IDC_RADIO2)) select_kind = 2;
			else if (IsDlgButtonChecked(hDlg, IDC_RADIO3)) select_kind = 1;
			else return TRUE;

			GetDlgItemText(hDlg, IDC_EDIT1, buf, 21);
			SetFocus(hIdInputBox);
			ZeroMemory(cl_id, 20);
			
			buf_len = (int)wcslen(buf);
			if (buf_len == 0) return TRUE;
			wcstombs(cl_id, buf, buf_len + 1);

			// 접속 요청 보내야댐
			DWORD iobyte;
			my_packet->size = sizeof(cs_packet_login);
			send_wsabuf.len = sizeof(cs_packet_login);
			my_packet->player_kind = select_kind;
			my_packet->type = CS_LOGIN;
			ZeroMemory(my_packet->player_id, 10);
			strcpy(my_packet->player_id, cl_id);

			ret = WSASend(g_mysocket, &send_wsabuf, 1, &iobyte, 0, NULL, NULL);
			if (ret) {
				int error_code = WSAGetLastError();
				printf("Error while sending packet [%d]", error_code);
			}

			EnableWindow(hSendButton, FALSE);

			EndDialog(hDlg, IDCANCEL);

			SendMessage(hIdInputBox, EM_SETSEL, 0, -1);
			return TRUE;

		case IDCANCEL:
			EndDialog(hDlg, IDCANCEL);
			return TRUE;
		}
		return FALSE;
	}
	return FALSE;
}


LRESULT CALLBACK WindowProc(HWND hwnd,
	UINT msg,
	WPARAM wparam,
	LPARAM lparam)
{
	// this is the main message handler of the system
	PAINTSTRUCT	ps;		   // used in WM_PAINT
	HDC			hdc;	   // handle to a device context

						   // what is the message 
	switch (msg)
	{
	case WM_CHAR :
	{
		if (wparam == 'A' || wparam == 'a') {
			cs_packet_attack *my_packet = reinterpret_cast<cs_packet_attack *>(send_buffer);
			my_packet->size = sizeof(my_packet);
			my_packet->type = CS_ATTACK;
			send_wsabuf.len = sizeof(my_packet);
			DWORD iobyte;
			WSASend(g_mysocket, &send_wsabuf, 1, &iobyte, 0, NULL, NULL);
		}
	}
		break;
	case WM_KEYDOWN: {
		if (wparam == VK_RETURN);

		int x = 0, y = 0;
		if (wparam == VK_RIGHT)	x += 1;
		if (wparam == VK_LEFT)	x -= 1;
		if (wparam == VK_UP)	y -= 1;
		if (wparam == VK_DOWN)	y += 1;
		cs_packet_move *my_packet = reinterpret_cast<cs_packet_move *>(send_buffer);
		my_packet->size = sizeof(my_packet);
		my_packet->type = CS_MOVE;
		send_wsabuf.len = sizeof(my_packet);
		DWORD iobyte;
		if (0 != x) {
			if (1 == x) my_packet->direction = 3;
			else my_packet->direction = 2;
			int ret = WSASend(g_mysocket, &send_wsabuf, 1, &iobyte, 0, NULL, NULL);
			if (ret) {
				int error_code = WSAGetLastError();
				printf("Error while sending packet [%d]", error_code);
			}
		}
		if (0 != y) {
			if (1 == y) my_packet->direction = 1;
			else my_packet->direction = 0;
			WSASend(g_mysocket, &send_wsabuf, 1, &iobyte, 0, NULL, NULL);
		}


	}
					 break;
	case WM_CREATE:
	{
		// do initialization stuff here

		return(0);
	} break;

	case WM_PAINT:
	{
		// start painting
		hdc = BeginPaint(hwnd, &ps);

		// end painting
		EndPaint(hwnd, &ps);
		return(0);
	} break;

	case WM_DESTROY:
	{
		// kill the application			
		PostQuitMessage(0);
		return(0);
	} break;
	case WM_SOCKET:
	{
		if (WSAGETSELECTERROR(lparam)) {
			closesocket((SOCKET)wparam);
			clienterror();
			break;
		}
		switch (WSAGETSELECTEVENT(lparam)) {
		case FD_READ:
			ReadPacket((SOCKET)wparam);
			break;
		case FD_CLOSE:
			closesocket((SOCKET)wparam);
			clienterror();
			break;
		}
	}

	default:break;

	} // end switch

	  // process any messages that we didn't take care of 
	return (DefWindowProc(hwnd, msg, wparam, lparam));

} // end WinProc

  // WINMAIN ////////////////////////////////////////////////

int WINAPI WinMain(HINSTANCE hinstance,
	HINSTANCE hprevinstance,
	LPSTR lpcmdline,
	int ncmdshow)
{
	// this is the winmain function

	WNDCLASS winclass;	// this will hold the class we create
	HWND	 hwnd;		// generic window handle
	MSG		 msg;		// generic message


						// first fill in the window class stucture
	winclass.style = CS_DBLCLKS | CS_OWNDC |
		CS_HREDRAW | CS_VREDRAW;
	winclass.lpfnWndProc = WindowProc;
	winclass.cbClsExtra = 0;
	winclass.cbWndExtra = 0;
	winclass.hInstance = hinstance;
	winclass.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	winclass.hCursor = LoadCursor(NULL, IDC_ARROW);
	winclass.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
	winclass.lpszMenuName = NULL;
	winclass.lpszClassName = WINDOW_CLASS_NAME;

	// register the window class
	if (!RegisterClass(&winclass))
		return(0);

	// create the window, note the use of WS_POPUP
	if (!(hwnd = CreateWindow(WINDOW_CLASS_NAME, // class
		L"Chess Client",	 // title
		WS_OVERLAPPEDWINDOW | WS_VISIBLE,
		50, 50,	   // x,y
		WINDOW_WIDTH,  // width
		WINDOW_HEIGHT, // height
		NULL,	   // handle to parent 
		NULL,	   // handle to menu
		hinstance,// instance
		NULL)))	// creation parms
		return(0);

	// save the window handle and instance in a global
	main_window_handle = hwnd;
	main_instance = hinstance;

	// perform all game console specific initialization
	Game_Init();

	DialogBox(hinstance, MAKEINTRESOURCE(IDD_DIALOG1), NULL, DlgProc);

	// enter main event loop
	while (1)
	{
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			// test if this is a quit
			if (msg.message == WM_QUIT)
				break;

			// translate any accelerator keys
			TranslateMessage(&msg);

			// send the message to the window proc
			DispatchMessage(&msg);
		} // end if

		  // main game processing goes here
		Game_Main();

	} // end while

	  // shutdown game and release all resources
	Game_Shutdown();

	// return to Windows like this
	return(msg.wParam);

} // end WinMain

  ///////////////////////////////////////////////////////////

  // WINX GAME PROGRAMMING CONSOLE FUNCTIONS ////////////////

int Game_Init(void *parms)
{
	// this function is where you do all the initialization 
	// for your game

	// init my info
	g_myid = 0;
	g_myexp = 10;
	g_mygold = 10000;
	g_mytype = PLAYER;
	g_mykind = FAIRY;
	g_mylevel = 10;
	g_myhp = 8250;
	g_myattack = 1024;
	is_login_ok = false;


	// set up screen dimensions
	screen_width = WINDOW_WIDTH;
	screen_height = WINDOW_HEIGHT;
	screen_bpp = 32;

	// initialize directdraw
	DD_Init(screen_width, screen_height, screen_bpp);


	// create and load the reactor bitmap image
	Create_Bitmap32(&reactor, 0, 0, 256, 257);
	Create_Bitmap32(&grass_tile, 0, 0, 128, 32);
	Create_Bitmap32(&sand_tile, 0, 0, 128, 32);
	Load_Image_Bitmap32(&reactor, L"CHESSMAP.BMP", 0, 0, BITMAP_EXTRACT_MODE_ABS);
	Load_Image_Bitmap32(&grass_tile, L"TA_tile.BMP", 0, 0, BITMAP_EXTRACT_MODE_ABS);
	grass_tile.x = 0;
	grass_tile.y = 0;
	grass_tile.height = TILE_WIDTH;
	grass_tile.width = TILE_WIDTH;
	Load_Image_Bitmap32(&sand_tile, L"TA_tile.BMP", 0, 0, BITMAP_EXTRACT_MODE_ABS);
	sand_tile.x = 64;
	sand_tile.y = 0;
	sand_tile.height = TILE_WIDTH;
	sand_tile.width = TILE_WIDTH;


	// now let's load in all the frames for the skelaton!!!

	Load_Texture(L"TA_player.PNG", UNIT_TEXTURE, 128, 32);


	if (!Create_BOB32(&player, 0, 0, 32, 32, 1, BOB_ATTR_SINGLE_FRAME)) return(0);
	Load_Frame_BOB32(&player, UNIT_TEXTURE, 0, 1, 0, BITMAP_EXTRACT_MODE_CELL);

	// set up stating state of skelaton
	Set_Animation_BOB32(&player, 0);
	Set_Anim_Speed_BOB32(&player, 2);
	Set_Vel_BOB32(&player, 0, 0);
	Set_Pos_BOB32(&player, 50, 50);


	// create skelaton bob
	for (int i = 0; i < MAX_USER; ++i) {
		if (!Create_BOB32(&skelaton[i], 0, 0, 32, 32, 1, BOB_ATTR_SINGLE_FRAME))
			return(0);
		Load_Frame_BOB32(&skelaton[i], UNIT_TEXTURE, 0, 0, 0, BITMAP_EXTRACT_MODE_CELL);

		// set up stating state of skelaton
		Set_Animation_BOB32(&skelaton[i], 0);
		Set_Anim_Speed_BOB32(&skelaton[i], 2);
		Set_Vel_BOB32(&skelaton[i], 0, 0);
		Set_Pos_BOB32(&skelaton[i], 50, 50);
	}

	Load_Texture(L"TA_monster.PNG", UNIT_TEXTURE, 288, 32);

	// create skelaton bob
	for (int npc_id = 0; npc_id < NUM_NPC - 4; ++npc_id) {
		if (npc_id < 1500) {
			if (!Create_BOB32(&npc[npc_id], 0, 0, 32, 32, 1, BOB_ATTR_SINGLE_FRAME))
				return(0);
			Load_Frame_BOB32(&npc[npc_id], UNIT_TEXTURE, 0, 0, 0, BITMAP_EXTRACT_MODE_CELL);
		}
		else if (npc_id >= 1500 && npc_id < 3000) {
			if (!Create_BOB32(&npc[npc_id], 0, 0, 32, 32, 1, BOB_ATTR_SINGLE_FRAME))
				return(0);

			Load_Frame_BOB32(&npc[npc_id], UNIT_TEXTURE, 0, 3, 0, BITMAP_EXTRACT_MODE_CELL);
		}
		else if (npc_id >= 3000 && npc_id < 4500) {
			if (!Create_BOB32(&npc[npc_id], 0, 0, 32, 32, 1, BOB_ATTR_SINGLE_FRAME))
				return(0);

			Load_Frame_BOB32(&npc[npc_id], UNIT_TEXTURE, 0, 6, 0, BITMAP_EXTRACT_MODE_CELL);
		}

		else if (npc_id >= 4500 && npc_id < 5250) {
			if (!Create_BOB32(&npc[npc_id], 0, 0, 32, 32, 1, BOB_ATTR_SINGLE_FRAME))
				return(0);

			Load_Frame_BOB32(&npc[npc_id], UNIT_TEXTURE, 0, 1, 0, BITMAP_EXTRACT_MODE_CELL);
		}
		else if (npc_id >= 5250 && npc_id < 6000) {
			if (!Create_BOB32(&npc[npc_id], 0, 0, 32, 32, 1, BOB_ATTR_SINGLE_FRAME))
				return(0);

			Load_Frame_BOB32(&npc[npc_id], UNIT_TEXTURE, 0, 4, 0, BITMAP_EXTRACT_MODE_CELL);
		}
		else if (npc_id >= 6000 && npc_id < 6750) {
			if (!Create_BOB32(&npc[npc_id], 0, 0, 32, 32, 1, BOB_ATTR_SINGLE_FRAME))
				return(0);

			Load_Frame_BOB32(&npc[npc_id], UNIT_TEXTURE, 0, 7, 0, BITMAP_EXTRACT_MODE_CELL);
		}

		else if (npc_id >= 6750 && npc_id < 7050) {
			if (!Create_BOB32(&npc[npc_id], 0, 0, 32, 32, 1, BOB_ATTR_SINGLE_FRAME))
				return(0);

			Load_Frame_BOB32(&npc[npc_id], UNIT_TEXTURE, 0, 2, 0, BITMAP_EXTRACT_MODE_CELL);
		}
		else if (npc_id >= 7050 && npc_id < 7350) {
			if (!Create_BOB32(&npc[npc_id], 0, 0, 32, 32, 1, BOB_ATTR_SINGLE_FRAME))
				return(0);
			Load_Frame_BOB32(&npc[npc_id], UNIT_TEXTURE, 0, 5, 0, BITMAP_EXTRACT_MODE_CELL);
		}
		else if (npc_id >= 7350 && npc_id < 7650) {
			if (!Create_BOB32(&npc[npc_id], 0, 0, 32, 32, 1, BOB_ATTR_SINGLE_FRAME))
				return(0);
			Load_Frame_BOB32(&npc[npc_id], UNIT_TEXTURE, 0, 8, 0, BITMAP_EXTRACT_MODE_CELL);
		}

		// set up stating state of skelaton
		Set_Animation_BOB32(&npc[npc_id], 0);
		Set_Anim_Speed_BOB32(&npc[npc_id], 2);
		Set_Vel_BOB32(&npc[npc_id], 0, 0);
		Set_Pos_BOB32(&npc[npc_id], 0, 0);
		// Set_ID(&npc[i], i);
	}

	Load_Texture(L"TA_npc.PNG", UNIT_TEXTURE, 256, 64);

	for (int npc_id = 7651; npc_id < NUM_NPC; ++npc_id) {
		if (!Create_BOB32(&npc[npc_id], 0, 0, 64, 64, 1, BOB_ATTR_SINGLE_FRAME))
			return(0);

		if (npc_id == 7651) {
			Load_Frame_BOB32(&npc[npc_id], UNIT_TEXTURE, 0, 0, 0, BITMAP_EXTRACT_MODE_CELL);
		}
		else if (npc_id == 7652) {
			Load_Frame_BOB32(&npc[npc_id], UNIT_TEXTURE, 0, 1, 0, BITMAP_EXTRACT_MODE_CELL);
		}
		else if (npc_id == 7653) {
			Load_Frame_BOB32(&npc[npc_id], UNIT_TEXTURE, 0, 2, 0, BITMAP_EXTRACT_MODE_CELL);
		}


		Set_Animation_BOB32(&npc[npc_id], 0);
		Set_Anim_Speed_BOB32(&npc[npc_id], 2);
		Set_Vel_BOB32(&npc[npc_id], 0, 0);
		Set_Pos_BOB32(&npc[npc_id], 0, 0);

	}

	Load_Texture(L"TA_npc.PNG", UNIT_TEXTURE, 384, 96);
	if (!Create_BOB32(&npc[7650], 0, 0, 96, 96, 1, BOB_ATTR_SINGLE_FRAME))
		return(0);

	Load_Frame_BOB32(&npc[7650], UNIT_TEXTURE, 0, 3, 0, BITMAP_EXTRACT_MODE_CELL);

	Set_Animation_BOB32(&npc[7650], 0);
	Set_Anim_Speed_BOB32(&npc[7650], 2);
	Set_Vel_BOB32(&npc[7650], 0, 0);
	Set_Pos_BOB32(&npc[7650], 0, 0);

	// set clipping rectangle to screen extents so mouse cursor
	// doens't mess up at edges
	//RECT screen_rect = {0,0,screen_width,screen_height};
	//lpddclipper = DD_Attach_Clipper(lpddsback,1,&screen_rect);

	// hide the mouse
	//ShowCursor(FALSE);

	WSADATA	wsadata;
	WSAStartup(MAKEWORD(2, 2), &wsadata);

	g_mysocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, 0);

	SOCKADDR_IN ServerAddr;
	ZeroMemory(&ServerAddr, sizeof(SOCKADDR_IN));
	ServerAddr.sin_family = AF_INET;
	ServerAddr.sin_port = htons(SERVER_PORT);
	ServerAddr.sin_addr.s_addr = inet_addr("127.0.0.1");

	int Result = WSAConnect(g_mysocket, (sockaddr *)&ServerAddr, sizeof(ServerAddr), NULL, NULL, NULL, NULL);

	WSAAsyncSelect(g_mysocket, main_window_handle, WM_SOCKET, FD_CLOSE | FD_READ);

	send_wsabuf.buf = send_buffer;
	send_wsabuf.len = BUF_SIZE;
	recv_wsabuf.buf = recv_buffer;
	recv_wsabuf.len = BUF_SIZE;

	

	// return success
	return(1);

} // end Game_Init

  ///////////////////////////////////////////////////////////

int Game_Shutdown(void *parms)
{
	// this function is where you shutdown your game and
	// release all resources that you allocated

	// kill the reactor
	Destroy_Bitmap32(&grass_tile);
	Destroy_Bitmap32(&sand_tile);
	Destroy_Bitmap32(&reactor);

	// kill skelaton
	for (int i = 0; i < MAX_USER; ++i) Destroy_BOB32(&skelaton[i]);
	for (int i = 0; i < NUM_NPC; ++i)
		Destroy_BOB32(&npc[i]);

	// shutdonw directdraw
	DD_Shutdown();

	WSACleanup();

	// return success
	return(1);
} // end Game_Shutdown

  ///////////////////////////////////////////////////////////

int Game_Main(void *parms)
{
	// this is the workhorse of your game it will be called
	// continuously in real-time this is like main() in C
	// all the calls for you game go here!
	// check of user is trying to exit
	if (KEY_DOWN(VK_ESCAPE) || KEY_DOWN(VK_SPACE))
		PostMessage(main_window_handle, WM_DESTROY, 0, 0);

	// start the timing clock
	Start_Clock();

	// clear the drawing surface
	DD_Fill_Surface(D3DCOLOR_ARGB(255, 0, 0, 0));

	// get player input

	g_pd3dDevice->BeginScene();
	g_pSprite->Begin(D3DXSPRITE_ALPHABLEND | D3DXSPRITE_SORT_TEXTURE);

	// draw the background reactor image
	for (int i = 0; i<20; ++i)
		for (int j = 0; j<20; ++j)
		{
			int tile_x = i + g_left_x;
			int tile_y = j + g_top_y;
			if ((tile_x <0) || (tile_y<0)) continue;
			if (((tile_x >> 2) % 2) == ((tile_y >> 2) % 2))
				Draw_Bitmap32(&sand_tile, TILE_WIDTH * i + 30, TILE_WIDTH * j + 60);
			else
				Draw_Bitmap32(&grass_tile, TILE_WIDTH * i + 30, TILE_WIDTH * j + 60);
		}
	//	Draw_Bitmap32(&reactor);

	g_pSprite->End();
	g_pSprite->Begin(D3DXSPRITE_ALPHABLEND | D3DXSPRITE_SORT_TEXTURE);


	// draw the skelaton
	Draw_BOB32(&player);
	for (int i = 0; i<MAX_USER; ++i) Draw_BOB32(&skelaton[i]);
	for (int i = 0; i < NUM_NPC; ++i) Draw_BOB32(&npc[i]);

	// draw some text
	wchar_t namebuf[20];
	mbstowcs(namebuf, cl_id, strlen(cl_id)+1);
	wchar_t ui[100];
	wsprintf(ui, L"┌────────────────────────────┐");
	Draw_Text_D3D(ui, 30, 5, D3DCOLOR_ARGB(255, 255, 255, 0));
	wsprintf(ui, L"┌──────────── SYSTEM ───────────┐");
	Draw_Text_D3D(ui, 30, WINDOW_HEIGHT - 120, D3DCOLOR_ARGB(255, 255, 255, 0));

	wchar_t ui_text[300];
	wsprintf(ui_text, L"  %10s │ Lv. %2d │ HP %4d │ EXP %6d │ %5dG  ", namebuf, g_mylevel, g_myhp, g_myexp, g_mygold);
	Draw_Text_D3D(ui_text, 30, 20, D3DCOLOR_ARGB(255, 255, 255, 255));

	wsprintf(ui, L"└────────────────────────────┘");
	Draw_Text_D3D(ui, 30, 35, D3DCOLOR_ARGB(255, 255, 255, 0));
	Draw_Text_D3D(ui, 30, WINDOW_HEIGHT - 60, D3DCOLOR_ARGB(255, 255, 255, 0));

	wchar_t text[300];
	wsprintf(text, L"MY POSITION (%3d, %3d)", player.x, player.y);
	Draw_Text_D3D(text, 30, WINDOW_HEIGHT - 135, D3DCOLOR_ARGB(255, 255, 255, 255));

	g_pSprite->End();
	g_pd3dDevice->EndScene();

	// flip the surfaces
	DD_Flip();

	// sync to 3o fps
	//Wait_Clock(30);


	// return success
	return(1);

} // end Game_Main

  //////////////////////////////////////////////////////////