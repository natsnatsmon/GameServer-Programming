myid = 9999;
mytype = 9999;
mykind = 9999;
mytarget = -1;

function set_npc_info(id, type, kind)
	myid = id;
	mytype = type;
	mykind = kind
end;

function set_target_user(user_id)
	mytarget = user_id;
end;

function event_player_move (player)
	player_x = API_get_player_x(player);
	player_y = API_get_player_y(player);
	my_x = API_get_npc_x(myid);
	my_y = API_get_npc_y(myid);
	if (player_x == my_x) then
		if (player_y == my_y) then
			API_SendMessage(player, myid, "HELLO");
		end
	end

end

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
