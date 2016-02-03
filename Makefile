CPP = g++

#for Linux
CFLAGS = -std=c++11 -Wall -O2 -D_FILE_OFFSET_BITS=64
LFLAGS = -lfuse -lgcrypt -lboost_system -lssl -lcrypto -lpthread
OBJS = main.o CBlockIO.o CSimpleFS.o CEncrypt.o CNetBlockIO.o CCacheIO.o CDirectory.o fuseoper.o INode.o CWriteRingBuffer.o

# for windows
#CFLAGS = -std=gnu++11 -Wall -O2 -D_FILE_OFFSET_BITS=64 -Iresources/dokan/include
#LFLAGS = -lgcrypt -lboost_system -lssl -lcrypto -lpthread resources/dokan/dokan.dll
#OBJS = main.o CBlockIO.o CSimpleFS.o CEncrypt.o CNetBlockIO.o CCacheIO.o CDirectory.o dokanoper.o INode.o CWriteRingBuffer.o

.PHONY: clean check

coverfs: $(OBJS) coverfsserver
	$(CPP) -o coverfs $(OBJS) $(LFLAGS)

coverfsserver: src/coverfsserver.cpp
	$(CPP) $(CFLAGS) -o coverfsserver src/coverfsserver.cpp $(LFLAGS)

main.o: src/main.cpp
	$(CPP) $(CFLAGS) -c src/main.cpp

CBlockIO.o: src/CBlockIO.h src/CBlockIO.cpp
	$(CPP) $(CFLAGS) -c src/CBlockIO.cpp

CEncrypt.o: src/CEncrypt.h src/CEncrypt.cpp
	$(CPP) $(CFLAGS) -c src/CEncrypt.cpp

CNetBlockIO.o: src/CNetBlockIO.h src/CNetBlockIO.cpp
	$(CPP) $(CFLAGS) -c src/CNetBlockIO.cpp

CCacheIO.o: src/CCacheIO.h src/CCacheIO.cpp
	$(CPP) $(CFLAGS) -c src/CCacheIO.cpp

CDirectory.o: src/CDirectory.h src/CDirectory.cpp
	$(CPP) $(CFLAGS) -c src/CDirectory.cpp

CSimpleFS.o: src/CSimpleFS.h src/CSimpleFS.cpp
	$(CPP) $(CFLAGS) -c src/CSimpleFS.cpp

INode.o: src/INode.h src/INode.cpp
	$(CPP) $(CFLAGS) -c src/INode.cpp

CWriteRingBuffer.o: src/CWriteRingBuffer.h src/CWriteRingBuffer.cpp
	$(CPP) $(CFLAGS) -c src/CWriteRingBuffer.cpp

fuseoper.o: src/fuseoper.cpp
	$(CPP) $(CFLAGS) -c src/fuseoper.cpp

dokanoper.o: src/dokanoper.cpp
	$(CPP) $(CFLAGS) -c src/dokanoper.cpp

clean:
	rm -f *.o
	rm -f coverfs
	rm -f coverfsserver
	rm -f checkfragment

check: tests/checkfragment.cpp
	$(CPP) -o checkfragment tests/checkfragment.cpp $(LFLAGS)

archive:
	tar -cvJf coverfs.tar.xz --transform 's,^,CoverFS/,' --show-transformed \
	src/*.cpp 		\
	src/*.h 		\
	src/qt/*		\
	ssl/*			\
	tests/*.cpp 		\
	resources		\
	Makefile		\
	README.md
