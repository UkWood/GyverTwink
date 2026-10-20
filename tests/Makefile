# Хост-тесты прошивки: make -C tests
CXX ?= g++
CXXFLAGS = -std=c++17 -g -O1 -Wall -fsanitize=address,undefined -fno-sanitize-recover=undefined

test: test_firmware
	./test_firmware

test_firmware: test_firmware.cpp host_stubs.h minitest.h $(wildcard ../firmware/GyverTwink_v1.2/*.ino) ../firmware/GyverTwink_v1.2/types.h ../firmware/GyverTwink_v1.2/Timer.h
	$(CXX) $(CXXFLAGS) $< -o $@

clean:
	rm -f test_firmware

.PHONY: test clean
