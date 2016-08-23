.PHONY: all test clean deps tags 

#CXX=g++
CXX=cl.exe
#CXXFLAGS += -g -Wall
CXXFLAGS += -c -MD -Zi -EHsc -GR -DWIN32 -DWIN64 -D_WIN64 -DWINDOWS
#LDFLAGS += -pthread
LDFLAGS += -debug

LDFLAGS_EXE += $(LDFLAGS) -subsystem:console
LDFLAGS_DLL += $(LDFLAGS) -dll

LD=link.exe

DEPS_INCLUDE_PATH=-I deps/json-cpp/include/ -I deps/http-parser/
DEPS_LIB_PATH=deps/json-cpp/output/lib/json_libmt.lib deps/http-parser/http_parser.lib
SRC_INCLUDE_PATH=-I src
OUTPUT_INCLUDE_PATH=-I output/include
OUTPUT_LIB_PATH=output/lib/simpleserver.lib

objects := $(patsubst %.cpp,%.o,$(wildcard src/*.cpp))

all: prepare deps simpleserver.lib
	cp src/*.h output/include/
	mv simpleserver.lib output/lib/

prepare: 
	mkdir -p output/include output/lib output/bin

tags:
	ctags -R /usr/include src deps

deps:
	make -C deps/http-parser
	make -C deps/json-cpp

simpleserver.lib: $(objects)
#	ar -rcs simpleserver.lib src/*.o
	lib -out:$@ src/*.o

test: all http_server_test.exe http_parser_test.exe

%.o: %.cpp
	$(CXX) -c $(CXXFLAGS) $(DEPS_INCLUDE_PATH) $(SRC_INCLUDE_PATH) $< -Fo$@

%.o: test/%.cpp
	$(CXX) -c $(CXXFLAGS) $(DEPS_INCLUDE_PATH) $(SRC_INCLUDE_PATH) $< -Fo$@

http_server_test.exe: test/http_server_test.o
	$(LD) -debug $(LDFLAGS_EXE) $< $(OUTPUT_LIB_PATH) $(DEPS_LIB_PATH) -out:output/bin/$@
	mt.exe -manifest output/bin/$@.manifest -outputresource:'output/bin/$@;1'

http_parser_test.exe: test/http_parser_test.o
	$(LD) -debug $(LDFLAGS_EXE) $< $(OUTPUT_LIB_PATH) $(DEPS_LIB_PATH) -out:output/bin/$@
	mt.exe -manifest output/bin/$@.manifest -outputresource:'output/bin/$@;1'

clean:
	make -C deps/http-parser clean
	make -C deps/json-cpp clean
	rm -rf src/*.o
	rm -rf test/*.o
	rm -rf output/*
	rm -rf vc*.pdb
