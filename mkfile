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
	float64.$O\
	table.$O\
	dict.$O\
	dict_native.$O\
	file_native.$O\
	template_render.$O\
	markdown.$O\


HFILES=chunk.h\
	memory.h\
	debug.h\
	value.h\
	vm.h\
	scanner.h\
	compiler.h\
	object.h\
	float64.h\
	table.h\
	dict.h\
	file_native.h\
	template_render.h\
	markdown.h\

BIN=$home/bin/$objtype

lux.$O: version.h buildstamp.h

buildstamp.h:Q: stamp.force stamp.rc
	rc stamp.rc >$target.tmp
	if(! cmp -s $target.tmp $target)
		mv $target.tmp $target
	rm -f $target.tmp

stamp.force:VQ:
	status=''

</sys/src/cmd/mkone


