#include <iostream>
#include <string.h>

// C++ �ƴϰ� C �� ... C++�̸� Cpp��� ���� ��
extern "C" {
#include "include\lua.h"
#include "include\lauxlib.h"
#include "include\lualib.h"
}

void lua_error(const char *msg, const char *err) {
	std::cout << msg << err << std::endl;
}

int addnum_c(lua_State *L) {
	int a = (int)lua_tonumber(L, -1);
	int b = (int)lua_tonumber(L, -2);
	int result = a + b;
	lua_pop(L, 3);

	lua_pushnumber(L, result);
	return 1;
}

int main() {

	// ��� ����ӽ��� ���� �����Ѵ�.. �װ� �����ͷ� �޴´�.. ����ӽ��� ����� ������ ���� ��
	lua_State *L = luaL_newstate();

	// ǥ�� ���̺귯�� �Լ�����(print ��) ����ӽſ� ��� �־��ش�
	luaL_openlibs(L);

	// -=-=-=-=-=-=-=-=-=-=-=-=

	int orc_hp, orc_mp;

	// ������� ������ ��� �ϴ���
	int err = luaL_loadfile(L, "orc.lua");
	if (0 != err) lua_error("Error in main() : ", lua_tostring(L, -1));

	err = lua_pcall(L, 0, 0, 0);
	if (0 != err) lua_error("Error in main() : ", lua_tostring(L, -1));

	// ����� ��� ��������. ���� ����� ����ӽſ� ����Ǿ������� �̾ƿ��� �ǿ�
	// stack�� �� ���� �÷� ���ƶ� ��� �Լ���... ���ƤѤ� �̾Ƽ� �������ִ°� �ƴ�.....
	// C�ϰ� ��ƴ� stack���� ����Ѵ�~~ �׷��� ���ÿ��� ���� �о�;���!
	lua_getglobal(L, "hp");
	lua_getglobal(L, "mp");
	// ��������Ʈ�� ����Ű�� �ͺ��� 2�� �ؿ��ִ°� �����ض�
	orc_hp = (int)lua_tonumber(L, -2);
	orc_mp = (int)lua_tonumber(L, -1);

	std::cout << "orc_hp : " << orc_hp << "\ orc_mp : " << orc_mp << '\n';
	// 2�� ���������ϱ� 2�� pop�������
	lua_pop(L, 2);


	lua_getglobal(L, "heal_event");
	// �Ķ���� �־�����
	lua_pushnumber(L, 30);
	// �Ķ���� �ϳ� �־������� �ι�° ���ڴ� 1~ (�Ķ���� ����) �̰� 0���� �ϸ� 30�� �����.. �ƹ��͵� ���µ�
	// ���ϰ��� �ϳ� ������ ����° ���ڵ� 1
	lua_pcall(L, 1, 1, 0);
	// ���ϰ��� ��� �޴���. ���ϰ��� ���ÿ� ���־��
	orc_hp = (int)lua_tonumber(L, -1);
	lua_pop(L, 1);
	

	std::cout << "orc_hp : " << orc_hp << "\ orc_mp : " << orc_mp << '\n';

	


	// register ����
	lua_register(L, "c_addnum", addnum_c);

	lua_getglobal(L, "addnum_lua");
	lua_pushnumber(L, 100); // a
	lua_pushnumber(L, 200); // b
	lua_pcall(L, 2, 1, 0); // L, �Ķ���� ����, ���� �� ����, 0

	int result = (int)lua_tonumber(L, -1);
	lua_pop(L, 1);

	std::cout << "Result = " << result << '\n';



	// LUA ���� �⺻ ����

	const char * buff = "print \"Hello, World!\"";
	// line������ ����ҰŴϱ� line~
	luaL_loadbuffer(L, buff, strlen(buff), "line");
	// ��ɾ �ָ� ������ �ϴ���? �׷��� �ʾƿ� �ε�������ϰ� ������ �����ʾƿ� ������ ����ϴ��� lua_pcall�� �մϴ�
	lua_pcall(L, 0, 0, 0);
	

	buff = "a = 1";
	luaL_loadbuffer(L, buff, strlen(buff), "line");
	lua_pcall(L, 0, 0, 0);

	buff = "b = a + 100";
	luaL_loadbuffer(L, buff, strlen(buff), "line");
	lua_pcall(L, 0, 0, 0);

	buff = "print(b)";
	luaL_loadbuffer(L, buff, strlen(buff), "line");
	lua_pcall(L, 0, 0, 0);

	
	// -=-=-=-=-=-=-=-=-=-=-=-=	
	
	lua_close(L);

	system("pause");
}