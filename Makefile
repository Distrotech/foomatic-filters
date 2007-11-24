
OBJECTS=foomaticrip.o options.o util.o

a.out: ${OBJECTS}
	cc -g -Wall ${OBJECTS} -o $@

.c.o:
	cc -c -g -Wall $? -o $@
