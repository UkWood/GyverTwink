# Хост-тесты прошивки: make -C tests        (юнит-тесты)
#                      make -C tests stand  (стенд протокола: Python-клиент против настоящего кода прошивки)
CXX ?= g++
CXXFLAGS = -std=c++17 -g -O1 -Wall -fsanitize=address,undefined -fno-sanitize-recover=undefined
DEPS = host_stubs.h host_env.h $(wildcard ../firmware/GyverTwink_v1.2/*.ino) ../firmware/GyverTwink_v1.2/types.h ../firmware/GyverTwink_v1.2/Timer.h ../firmware/GyverTwink_v1.2/web_page.h

test: test_firmware
	./test_firmware

stand: gt_host
	python3 protocol_stand.py ./gt_host

test_firmware: test_firmware.cpp test_web_cases.h test_scene_cases.h test_coords_cases.h test_vcalib_cases.h test_ota_cases.h minitest.h $(DEPS)
	$(CXX) $(CXXFLAGS) $< -o $@

gt_host: host_server.cpp $(DEPS)
	$(CXX) $(CXXFLAGS) -pthread $< -o $@

clean:
	rm -f test_firmware gt_host

.PHONY: test stand clean
