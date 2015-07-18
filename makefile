CC=g++-4.8
INCLUDEPATHS=-I./freetype-2.5.5/include/ -I$(SDKSTAGE)/opt/vc/include/ -I/opt/vc/include/interface/vcos/pthreads -I$(SDKSTAGE)/opt/vc/include/interface/vmcs_host/linux
CFLAGS=-std=c++11 -c -Wall $(INCLUDEPATHS)
LDFLAGS=-L$(SDKSTAGE)/opt/vc/lib/ -L./freetype-2.5.5/builds/unix/ -lfreetype -lGLESv2 -lEGL -lopenmaxil -lbcm_host -lvcos -lvchiq_arm -lpthread -lrt -lm
SOURCES=color.cpp dfr.cpp GamePad.cpp LodePNG.cpp main.cpp
OBJECTS=$(SOURCES:.cpp=.o)
EXECUTABLE=NESPI

all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS) 
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@

.cpp.o:
	$(CC) $(CFLAGS) $< -o $@
