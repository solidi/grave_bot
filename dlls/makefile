ifeq "$(CFG)" ""
CFG=release
endif
BUILD_DIR=$(CFG)

DEBUG_CFLAGS=
ifeq "$(CFG)" "debug"
	DEBUG_CFLAGS=-D_DEBUG -DDEBUG
endif

CPP = g++ -m32 -w -Wfatal-errors -std=c++11
TARGET = gravebot
ARCHFLAG = i686
BASEFLAGS = -Dstricmp=strcasecmp -Dstrcmpi=strcasecmp
OPTFLAGS = 
CPPFLAGS = ${BASEFLAGS} ${OPTFLAGS} ${DEBUG_CFLAGS} -march=${ARCHFLAG} -mtune=generic -msse2 -O2 -m32 -mfpmath=sse -finline-functions -pipe -w -I"../metamod" -I"../common" -I"../dlls" -I"../engine" -I"../pm_shared"
LDFLAGS = -Wl,--no-undefined -Wl,-z,defs

SRC = 	bot.cpp \
	bot_client.cpp \
	bot_combat.cpp \
	bot_navigate.cpp \
	bot_prophunt.cpp \
	bot_shidden.cpp \
	bot_start.cpp \
	bot_weapons.cpp \
	bot_weapons_hldm.cpp \
	bot_weapons_si.cpp \
	dll.cpp \
	engine.cpp \
	h_export.cpp \
	linkfunc.cpp \
	util.cpp \
	waypoint.cpp

OBJ = $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(SRC))

$(BUILD_DIR)/${TARGET}.so: ${OBJ}
	${CPP} -fPIC -shared -static ${LDFLAGS} -o $@ ${OBJ} -Xlinker -Map -Xlinker $(BUILD_DIR)/${TARGET}.map -ldl
	strip -s $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: %.cpp | $(BUILD_DIR)
	${CPP} ${CPPFLAGS} -c $< -o $@

clean:
	rm -f *.o
	rm -f $(BUILD_DIR)/*.o
	rm -f $(BUILD_DIR)/*.map
	rm -f $(BUILD_DIR)/${TARGET}.so

distclean:
	rm -rf $(BUILD_DIR)

.PHONY: clean distclean