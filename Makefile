TARGET_NAME := tuxracer
SRC := $(wildcard src/*.c)
RUNNER := runner/test_runner.c
MULTICORE ?= /home/Sajnaps/gb300/sf2000_multicore
MIPS_MUSL := /home/Sajnaps/toolchains/mipsel-linux-muslsf-cross/bin/mipsel-linux-muslsf-
MIPS_ELF := /opt/frog-toolchain/mipsel-mti-elf_stable/bin/mipsel-mti-elf-
QEMU := /home/Sajnaps/toolchains/qemu/usr/bin/qemu-mipsel

ifeq ($(platform),sf2000)
PLATFORM := sf2000
endif
ifeq ($(PLATFORM),sf2000)
PLATFORM := sf2000
endif

COMMON_INC := -Iinclude -Isrc

ifeq ($(PLATFORM),sf2000)
  CC := $(MIPS_ELF)gcc
  AR := $(MIPS_ELF)ar
  RANLIB := $(MIPS_ELF)ranlib
  BUILD := build_sf2000
  TARGET := $(TARGET_NAME)_libretro_sf2000.a
  CFLAGS := -EL -march=mips32 -mtune=mips32r2 -msoft-float -O2 -G0 \
            -mno-abicalls -fno-pic -ffreestanding -fno-strict-aliasing \
            -ffunction-sections -fdata-sections -fomit-frame-pointer \
            -fno-unwind-tables -fno-asynchronous-unwind-tables \
            -DSF2000 -DGB300 $(COMMON_INC)
  OBJS := $(patsubst src/%.c,$(BUILD)/%.o,$(SRC))
  all: $(TARGET)
$(TARGET): $(OBJS)
	$(AR) rcs $@ $(OBJS)
	$(RANLIB) $@
	@echo "Built $@"
$(BUILD)/%.o: src/%.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

else ifeq ($(PLATFORM),mips)
  CC := $(MIPS_MUSL)gcc
  BUILD := build_mips
  TARGET := runner/test_runner_mips
  CFLAGS := -EL -march=mips32 -mtune=mips32r2 -msoft-float -O2 -g -static -no-pie \
            -DTR_TEST_HOOKS -D_GNU_SOURCE $(COMMON_INC)
  all: $(TARGET)
$(TARGET): $(SRC) $(RUNNER)
	@mkdir -p runner
	$(CC) $(CFLAGS) -o $@ $(SRC) $(RUNNER)
	@echo "Built $@"

else
  CC := gcc
  BUILD := build_host
  TARGET := runner/test_runner
  CFLAGS := -O2 -g -std=gnu11 -DTR_TEST_HOOKS -D_GNU_SOURCE $(COMMON_INC)
  all: $(TARGET)
$(TARGET): $(SRC) $(RUNNER)
	@mkdir -p runner
	$(CC) $(CFLAGS) -o $@ $(SRC) $(RUNNER)
	@echo "Built $@"
endif

assets:
	python3 tools/convert_assets.py
	python3 tools/convert_tux_model.py

test: all
	@mkdir -p screenshots
	@rm -f screenshots/*.ppm screenshots/*.png
	./runner/test_runner assets/TuxRacer.gba screenshots/tux
	@set -e; for f in screenshots/*.ppm; do \
		ffmpeg -loglevel error -y -i "$$f" "$${f%.ppm}.png"; \
	done
	@ls -lh screenshots/*.png | tail -20

test-courses: all
	@mkdir -p screenshots/courses
	@rm -f screenshots/courses/*
	TUXRACER_COURSE_SHOTS=1 ./runner/test_runner assets/TuxRacer.gba screenshots/courses/c
	@set -e; for f in screenshots/courses/*.ppm; do \
		ffmpeg -loglevel error -y -i "$$f" "$${f%.ppm}.png"; \
	done

soak: all
	TUXRACER_SOAK=1 ./runner/test_runner assets/TuxRacer.gba screenshots/soak

mips: 
	$(MAKE) PLATFORM=mips all

test-mips: mips
	@mkdir -p screenshots/mips
	@rm -f screenshots/mips/*.ppm screenshots/mips/*.png
	$(QEMU) -L /home/Sajnaps/toolchains/mipsel-linux-muslsf-cross/mipsel-linux-muslsf \
		./runner/test_runner_mips assets/TuxRacer.gba screenshots/mips/tux
	@set -e; for f in screenshots/mips/*.ppm; do \
		ffmpeg -loglevel error -y -i "$$f" "$${f%.ppm}.png"; \
	done

device:
	@echo "== building Tux Racer cores =="
	$(MAKE) clean
	$(MAKE) -C $(MULTICORE) clean
	$(MAKE) -C $(MULTICORE) CORE=$(CURDIR) CONSOLE=$(TARGET_NAME) FROGGY_TYPE=SF2000
	@mkdir -p dist
	cp -f $(MULTICORE)/build/core_87000000 dist/tuxracer.sf2k
	@if [ -f $(MULTICORE)/build/bisrv.asd ]; then cp -f $(MULTICORE)/build/bisrv.asd dist/bisrv_sf2000.asd; fi
	$(MAKE) -C $(MULTICORE) clean
	$(MAKE) -C $(MULTICORE) CORE=$(CURDIR) CONSOLE=$(TARGET_NAME) FROGGY_TYPE=GB300V2
	cp -f $(MULTICORE)/build/core_87000000 dist/tuxracer.gb3k
	@if [ -f $(MULTICORE)/build/bisrv.asd ]; then cp -f $(MULTICORE)/build/bisrv.asd dist/bisrv_gb300.asd; fi
	python3 tools/package.py
	@ls -lh dist/

clean:
	rm -rf build_host build_mips build_sf2000 runner/test_runner runner/test_runner_mips \
	       $(TARGET_NAME)_libretro_sf2000.a

.PHONY: all assets test test-courses soak mips test-mips device clean
