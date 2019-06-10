#include <iostream>
#include <string.h>

// C++ 아니고 C 다 ... C++이면 Cpp라고 쓰면 됨
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

	// 루아 가상머신을 만들어서 리턴한다.. 그걸 포인터로 받는다.. 가상머신이 실행될 공간을 만든 것
	lua_State *L = luaL_newstate();

	// 표준 라이브러리 함수들을(print 등) 가상머신에 쥬욱 넣어준댜
	luaL_openlibs(L);

	// -=-=-=-=-=-=-=-=-=-=-=-=

	int orc_hp, orc_mp;

	// 루아파일 실행은 어떻게 하느냐
	int err = luaL_loadfile(L, "orc.lua");
	if (0 != err) lua_error("Error in main() : ", lua_tostring(L, -1));

	err = lua_pcall(L, 0, 0, 0);
	if (0 != err) lua_error("Error in main() : ", lua_tostring(L, -1));

	// 결과를 어떻게 얻어오느냐. 실행 결과가 가상머신에 저장되어있으니 뽑아오면 되요
	// stack의 맨 위에 올려 놓아라 라는 함수다... 값아ㅡㄹ 뽑아서 가져와주는게 아님.....
	// C하고 루아는 stack으로 통신한다~~ 그래서 스택에서 값을 읽어와야함!
	lua_getglobal(L, "hp");
	lua_getglobal(L, "mp");
	// 스택포인트가 가르키는 것보다 2개 밑에있는걸 리턴해라
	orc_hp = (int)lua_tonumber(L, -2);
	orc_mp = (int)lua_tonumber(L, -1);

	std::cout << "orc_hp : " << orc_hp << "\ orc_mp : " << orc_mp << '\n';
	// 2개 꺼내왔으니까 2개 pop해줘야쥐
	lua_pop(L, 2);


	lua_getglobal(L, "heal_event");
	// 파라미터 넣어주자
	lua_pushnumber(L, 30);
	// 파라미터 하나 넣어줬으니 두번째 인자는 1~ (파라미터 갯수) 이거 0으로 하면 30이 실행됨.. 아무것도 없는데
	// 리턴값도 하나 있으니 세번째 인자도 1
	lua_pcall(L, 1, 1, 0);
	// 리턴값은 어떻게 받느냐. 리턴값도 스택에 들어가있어용
	orc_hp = (int)lua_tonumber(L, -1);
	lua_pop(L, 1);
	

	std::cout << "orc_hp : " << orc_hp << "\ orc_mp : " << orc_mp << '\n';

	


	// register 연습
	lua_register(L, "c_addnum", addnum_c);

	lua_getglobal(L, "addnum_lua");
	lua_pushnumber(L, 100); // a
	lua_pushnumber(L, 200); // b
	lua_pcall(L, 2, 1, 0); // L, 파라미터 갯수, 리턴 값 갯수, 0

	int result = (int)lua_tonumber(L, -1);
	lua_pop(L, 1);

	std::cout << "Result = " << result << '\n';



	// LUA 실행 기본 연습

	const char * buff = "print \"Hello, World!\"";
	// line단위로 출력할거니까 line~
	luaL_loadbuffer(L, buff, strlen(buff), "line");
	// 명령어를 주면 실행을 하느냐? 그렇지 않아요 로드까지만하고 실행은 하지않아요 실행은 어떻게하느냐 lua_pcall로 합니당
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