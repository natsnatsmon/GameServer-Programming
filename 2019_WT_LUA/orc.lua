function calc_mp_from_hp(hp)
	local a
	a = 50
	return hp+a
end


hp = 100;
mp = calc_mp_from_hp(hp);


function heal_event(x)
	hp = hp + x
	return hp
end


function addnum_lua(a, b)
	return c_addnum(a, b)
end
