my_id = 9999;
my_state = 0;
my_target = -1;

my_name = "PIXIE";
my_kind = 0;
my_type = 1;
my_move_type = 1;

my_x = 150;
my_y = 150;

my_LEVEL = 5;
my_EXP = 25;
my_GOLD = 2;
my_HP = 10;
my_ATTACK = 10;


function set_npc_info(id, kind, type, move_type, x, y, lv, exp, g, hp, atk)
	my_id = id;
	my_state = 0;
	my_target = -1;

	my_name =;
	my_kind = kind;
	my_type = type;
	my_move_type = move_type;

	my_x = x;
	my_y = y;

	my_LEVEL = lv;
	my_EXP = exp;
	my_GOLD = g;
	my_HP = hp;
	my_ATTACK = atk;
end;

function load_npc_info()
	API_load_NPC_info(my_id, my_hp, my_x, my_y);
end;

function update_monster_info(curr_x, curr_y, curr_hp) {
	x = curr_x;
	y = curr_y;
	HP = curr_hp;
}

function set_target_player(player_id)
	my_target = player_id;
end;

function event_player_move (player)
	player_x = API_get_player_x(player);
	player_y = API_get_player_y(player);
	my_x = API_get_npc_x(myid);
	my_y = API_get_npc_y(myid);

	if(my_type == 2) then
		if(my_target == -1) then
			my_target = player;
		end;
	end;
end;

	if (player_x == my_x) then
		if (player_y == my_y) then
			API_SendMessage(player, myid, "HELLO");
		end
	end;

function event_npc_move(player)
	if(mystate == "escape") then
		if(myescstate > 0) then
			API_Move_NPC(myid, mystate, myescstate);	
		end
		if (myescstate <= 0) then
			API_SendMessage(player, myid, "BYE");
			API_Sleep_NPC(myid);
		end
	end
end
