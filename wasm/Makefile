

main:
	emcc -Os -Wall -DVMRP ../main.c ../fileLib.c ../network.c ../vmrp.c ../utils.c ../debug.c ../rbtree.c ../bridge.c ../memory.c  \
		./unicorn/libunicorn.a \
		-s EXPORTED_FUNCTIONS="['_main', '_malloc', '_free']" \
		-s ALLOW_MEMORY_GROWTH=1 \
		-s ALLOW_TABLE_GROWTH \
		-s USE_SDL=2 \
		-s USE_ZLIB=1 \
		-s WASM=1 \
		-s FORCE_FILESYSTEM=1 \
		-o dist/vmrp.js

		# 目前添加网络功能还有问题 https://github.com/emscripten-core/emscripten/issues/6009
		# -lwebsocket.js -s PROXY_POSIX_SOCKETS=1 -s USE_PTHREADS=1 -s PROXY_TO_PTHREAD=1 \


