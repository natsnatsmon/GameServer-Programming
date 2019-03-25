#pragma once
#include "Renderer.h"
#include "Object.h"
#include "Global.h"

// ScnMgr : 모든 렌더링, 오브젝트를 관리한다!
class SceneManager
{
private:
	Object *m_Objects[MAX_OBJECTS];
	Renderer *m_Renderer;

	GLuint m_PlayerTexture[MAX_PLAYER] = {0,};
	GLuint m_BoardTexture = 0;

public:
	SceneManager();
	~SceneManager();

	void RenderScene();

	void Update(float moveX, float moveY);

	void AddObject(int id, float x, float y, float z);
};
