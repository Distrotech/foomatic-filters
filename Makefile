
OBJECTS=foomaticrip.o options.o util.o

a.out: ${OBJECTS}
	cc -g -Wall -lm ${OBJECTS} -o $@

.c.o:
	cc -c -g -Wall $? -o $@
