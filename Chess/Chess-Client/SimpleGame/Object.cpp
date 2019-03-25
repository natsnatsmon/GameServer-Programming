#include "stdafx.h"
#include "Object.h"
#include <math.h>
#include <float.h>

Object::Object()
{
}


Object::~Object()
{
}

void Object::Update(float moveX, float moveY) {
}


void Object::GetPosition(float * x, float * y, float* z) { 
	*x = m_posX;
	*y = m_posY;
	*z = m_posZ;
}
void Object::SetPosition(float x, float y, float z) { 
	m_posX = x;
	m_posY = y;
	m_posZ = z;
}