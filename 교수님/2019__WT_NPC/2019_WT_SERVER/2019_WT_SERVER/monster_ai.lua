myid = 9999;

function set_uid(x)
	myid = x;
end;

function set_state(state)
	mystate = state;
end;

function set_escape_state(estate)
	myescstate = estate;
end;

function event_player_move (player)
	player_x = API_get_x(player);
	player_y = API_get_y(player);
	my_x = API_get_x(myid);
	my_y = API_get_y(myid);
	if (player_x == my_x) then
		if (player_y == my_y) then
			if(mystate == "sleep") then
				API_SendMessage(player, myid, "HELLO");
				API_Move_NPC(myid, mystate, myescstate);
			end
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
