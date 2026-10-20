# Хост-тесты прошивки: make -C tests        (юнит-тесты)
#                      make -C tests stand  (стенд протокола: Python-клиент против настоящего кода прошивки)
CXX ?= g++
CXXFLAGS = -std=c++17 -g -O1 -Wall -fsanitize=address,undefined -fno-sanitize-recover=undefined
DEPS = host_stubs.h host_env.h $(wildcard ../firmware/GyverTwink_v1.2/*.ino) ../firmware/GyverTwink_v1.2/types.h ../firmware/GyverTwink_v1.2/Timer.h

test: test_firmware
	./test_firmware

stand: gt_host
	python3 protocol_stand.py ./gt_host

test_firmware: test_firmware.cpp minitest.h $(DEPS)
	$(CXX) $(CXXFLAGS) $< -o $@

gt_host: host_server.cpp $(DEPS)
	$(CXX) $(CXXFLAGS) -pthread $< -o $@

clean:
	rm -f test_firmware gt_host

.PHONY: test stand clean
