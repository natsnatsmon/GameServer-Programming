#pragma once
class Object
{
private:
	float m_posX;
	float m_posY;
	float m_posZ;

public:
	Object();
	~Object();

	void Update(float moveX, float moveY);

	void GetPosition(float* x, float* y, float* z);
	void SetPosition(float x, float y, float z);
};

