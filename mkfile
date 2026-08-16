</$objtype/mkfile

TARG=lux
OFILES=lux.$O\
	chunk.$O\
	memory.$O\
	debug.$O\
	value.$O\
	vm.$O\
	scanner.$O\
	compiler.$O\
	object.$O\
	table.$O\
	dict.$O\
	dict_native.$O\
	template_render.$O\
	markdown.$O\
	draw.$O\
	draw_native.$O\
	plumb_native.$O\
	ninep.$O\
	ninep_native.$O\


HFILES=chunk.h\
	memory.h\
	debug.h\
	value.h\
	vm.h\
	scanner.h\
	compiler.h\
	object.h\
	table.h\
	dict.h\
	template_render.h\
	markdown.h\
	luxdraw.h\
	ninep.h\

# 9front: einit/eread are in libdraw. Bell Labs Plan 9 still has libevent.
LIB=/$objtype/lib/libdraw.a /$objtype/lib/libplumb.a `{if(test -f /$objtype/lib/libevent.a) echo /$objtype/lib/libevent.a}

BIN=$home/bin/$objtype

</sys/src/cmd/mkone
