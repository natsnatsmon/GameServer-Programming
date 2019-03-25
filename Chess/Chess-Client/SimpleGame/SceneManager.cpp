#include "stdafx.h"
#include "SceneManager.h"
#include <stdio.h>

SceneManager::SceneManager()
{
	// Initialize Renderer
	// Renderer는 창마다 하나씩만 만들면 된당!
	m_Renderer = NULL;

	m_Renderer = new Renderer(800, 800);

	if (!m_Renderer->IsInitialized())
	{
		std::cout << "Renderer could not be initialized.. \n";
	}

	// Load Texture
	for (int i = 0; i < MAX_PLAYER; ++i) {
		m_PlayerTexture[i] = m_Renderer->CreatePngTexture("player.png");
	}
	m_BoardTexture = m_Renderer->CreatePngTexture("board.png");

	for (int i = 0; i < MAX_OBJECTS; ++i) {
		m_Objects[i] = NULL;
	}

	// Init Test Obj
	m_Objects[PLAYER_ID] = new Object();
	m_Objects[PLAYER_ID]->SetPosition(50.f, 50.f, 0.f);
}


SceneManager::~SceneManager()
{	
	if (m_Renderer) {	
		delete[] m_Renderer;	
		m_Renderer = NULL;
	}
}

extern Packet recv_packet;
void SceneManager::RenderScene() {
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glClearColor(0.0f, 0.3f, 0.3f, 1.0f);
	
	m_Renderer->DrawTextureRect(0, 0, 0, 800.f, 800.f, 1, 1, 1, 1, m_BoardTexture);
	//std::cout << " player " << recv_packet.player_num << std::endl;
	//std::cout << " player x" << recv_packet.player_pos[0].x << std::endl;
	//std::cout << " player y" << recv_packet.player_pos[0].y << std::endl;
	for (int i = 0; i < recv_packet.player_num; ++i) {
		float x, y, z;
		x = recv_packet.player_pos[i].x;
		y = recv_packet.player_pos[i].y;
		z = 0.f;
		//	m_Objects[PLAYER_ID]->GetPosition(&x, &y, &z);

		m_Renderer->DrawTextureRect(x, y, z, 50.f, 100.f, 1, 1, 1, 1, m_PlayerTexture[i]);
	}
}

void SceneManager::Update(float moveX, float moveY) {
	m_Objects[PLAYER_ID]->Update(moveX, moveY);
}

void SceneManager::AddObject(int id, float x, float y, float z) {
	m_Objects[id] = new Object();
	m_Objects[id]->SetPosition(x, y, z);
}
