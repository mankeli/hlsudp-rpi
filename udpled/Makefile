OBJECTS=udp.o
BINARIES=udp
ALL_BINARIES=$(BINARIES) led-image-viewer

# Where our library resides. It is split between includes and the binary
# library in lib
RGB_LIB_DISTRIBUTION=../
RGB_INCDIR=$(RGB_LIB_DISTRIBUTION)/include
RGB_LIBDIR=$(RGB_LIB_DISTRIBUTION)/lib
RGB_LIBRARY_NAME=rgbmatrix
RGB_LIBRARY=$(RGB_LIBDIR)/lib$(RGB_LIBRARY_NAME).a
LDFLAGS+=-L$(RGB_LIBDIR) -l$(RGB_LIBRARY_NAME) -lrt -lm -lpthread
CXXFLAGS=-std=c++11 -Wall -O3 -g -I$(RGB_INCDIR)

all: udp

$(RGB_LIBRARY):
	$(MAKE) -C $(RGB_LIBDIR)

udp: $(OBJECTS) $(RGB_LIBRARY)
	$(CXX) $(CXXFLAGS) $(OBJECTS) -o $@ $(LDFLAGS)

clean:
	$(MAKE) -C lib clean
	$(MAKE) -C examples-api-use clean
	$(MAKE) -C utils clean
	$(MAKE) -C $(PYTHON_LIB_DIR) clean

