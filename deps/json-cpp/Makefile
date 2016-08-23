.PHONY: all test clean

SRC_INCLUDE_PATH=-I include -I src/lib_json

objects := $(patsubst %.cpp,%.o,$(wildcard src/lib_json/*.cpp))

all: json_libmt.lib
	echo "make all"
	mkdir -p output/include
	mkdir -p output/lib

	cp -r include/json ../../output/include/
#	mv libjson_libmt.a output/lib/
	mv json_libmt.lib output/lib

#libjson_libmt.a: $(objects)
json_libmt.lib: $(objects)
#	ar -rcs libjson_libmt.a src/lib_json/*.o
	lib -out:$@ src/lib_json/*.o

%.o: %.cpp
#	$(CXX) -c $(CXXFLAGS) $(SRC_INCLUDE_PATH) $< -o $@
	cl.exe -c -MD -Zi -EHsc -GR -DWIN32 -DWIN64 -D_WIN64 $(SRC_INCLUDE_PATH) $< -Fo$@

clean:
	rm -rf src/lib_json/*.o
