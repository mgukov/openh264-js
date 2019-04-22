TOTALMEMORY:=-s TOTAL_MEMORY=67108864
EMCC_OPTS=-O3 --llvm-lto 1 --memory-init-file 0 -s NO_DYNAMIC_EXECUTION=1 -s NO_FILESYSTEM=1

all: openh264.wasm.js

h264symbols='["_open_decoder","_close_decoder","_decode_h264buffer","_decode_nal"]'

openh264.wasm.js: openh264.js
	cat before.js openh264.js after.js > openh264.wasm.js

openh264.js: decoder.cpp openh264/libopenh264.a
	em++ -O3 --llvm-lto 3 --llvm-opts 3 $(TOTALMEMORY) -o $@ $^ -s EXTRA_EXPORTED_RUNTIME_METHODS='["ccall", "cwrap"]' \
		-s EXPORTED_FUNCTIONS='["_open_decoder","_close_decoder","_decode_nal"]' \
		-Iopenh264/codec/api/svc/ 

openh264:
	git clone https://github.com/cisco/openh264.git openh264

openh264/libopenh264.a: openh264
	cd openh264 && emmake make USE_ASM=False #OS=linux #ARCH=asmjs

.PHONY: clean cleanall distclean

clean:
	rm -f openh264.wasm.js openh264.js

cleanall: clean
	cd openh264; make clean

distclean: clean
	rm -Rf openh264 
