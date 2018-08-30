# Pitch Recognizer  Copyright (C) 2018  Lukáš Ondráček <ondracek.lukas@gmail.com>, use under GNU GPL3, see README file

CFLAGS   = -pthread -std=gnu99 -g
LDFLAGS  = -lm -lavcodec -lavformat -lavutil -lswresample -lportaudio

ifeq ($(arch), win32)
	CC       = i686-w64-mingw32-gcc
	CFLAGS  += -mthreads
	exeext   = .exe
	objext   = .obj
else
	exeext   =
	objext   = .o
endif


pitch_recognizer$(exeext): obj/main$(objext) obj/util$(objext) obj/player$(objext) obj/playerFile$(objext) obj/playerDevice$(objext) obj/drawerScale$(objext) obj/fft$(objext) obj/taskManager$(objext) obj/messages$(objext) obj/streamBuffer$(objext) obj/resources.gen$(objext) obj/mem$(objext) obj/pitchRecognition$(objext) obj/commandParser$(objext)
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS)

run: pitch_recognizer
	./pitch_recognizer

obj/%$(objext): src/%.c src/%.h
	mkdir obj -p
	$(CC) -c $< -o $@ $(CFLAGS)

messages=src/messagesList.gen.h src/messages.h

obj/main$(objext): src/player.h src/util.h $(messages) src/commandParser.h src/resources.gen.h src/mem.h
obj/fft$(objext): src/mem.h
obj/drawerScale$(objext): src/player.h $(messages) src/mem.h
obj/taskManager$(objext): src/util.h src/mem.h

obj/commandParser$(objext): $(messages) src/util.h src/mem.h
obj/messages$(objext): src/messagesList.gen.c $(messages) src/taskManager.h src/util.h src/mem.h
obj/player$(objext): src/taskManager.h src/util.h $(messages) src/playerFile.h src/playerDevice.h src/resources.gen.h src/mem.h
obj/playerFile$(objext): src/taskManager.h src/util.h $(messages) src/player.h src/playerDevice.h src/mem.h
obj/playerDevice$(objext): src/taskManager.h src/util.h $(messages) src/player.h $(messages) src/streamBuffer.h
obj/streamBuffer$(objext):
obj/util$(objext): src/mem.h

src/resources.gen.c: src/resourcesGen.pl VERSION src/intro.txt src/help.txt README COPYING
	src/resourcesGen.pl VERSION src/intro.txt src/help.txt README COPYING
src/resources.gen.h: src/resourcesGen.pl VERSION src/intro.txt src/help.txt README COPYING
	src/resourcesGen.pl VERSION src/intro.txt src/help.txt README COPYING

src/messagesList.gen.h: src/messagesList.pl src/messagesGen.pl
	src/messagesGen.pl
src/messagesList.gen.c: src/messagesList.pl src/messagesGen.pl src/*.h
	src/messagesGen.pl

clean:
	rm -rf obj pitch_recognizer pitch_recognizer.exe src/*.gen.*
