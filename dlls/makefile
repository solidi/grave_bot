ifeq "$(CFG)" ""
CFG=release
endif
BUILD_DIR=$(CFG)

CPP = g++-4.8 -m32
TARGET = gravebot
ARCHFLAG = i686
BASEFLAGS = -Dstricmp=strcasecmp -Dstrcmpi=strcasecmp
OPTFLAGS = 
CPPFLAGS = ${BASEFLAGS} ${OPTFLAGS} -march=${ARCHFLAG} -mtune=generic -msse2 -O2 -m32 -mfpmath=sse -finline-functions -pipe -w -I"../metamod" -I"../common" -I"../dlls" -I"../engine" -I"../pm_shared"

OBJ = 	bot.o \
	bot_client.o \
	bot_combat.o \
	bot_navigate.o \
	bot_start.o \
	bot_weapons.o \
	bot_weapons_hldm.o \
	bot_weapons_si.o \
	dll.o \
	engine.o \
	h_export.o \
	linkfunc.o \
	util.o \
	waypoint.o

${TARGET}.so: ${OBJ}
	${CPP} -fPIC -shared -static -o $@ ${OBJ} -Xlinker -Map -Xlinker ${TARGET}.map -ldl
	strip -s ${TARGET}.so 
	mv *.o $(BUILD_DIR)
	mv *.map $(BUILD_DIR)
	mv $@ $(BUILD_DIR)

clean:
	rm -f $(BUILD_DIR)/*.o
	rm -f $(BUILD_DIR)/*.map

distclean:
	rm -rf $(BUILD_DIR)
	mkdir $(BUILD_DIR)

%.o:	%.cpp
	${CPP} ${CPPFLAGS} -c $< -o $@

%.o:	%.c
	${CPP} ${CPPFLAGS} -c $< -o $@