BUILD_DIR ?= build
RELEASE_BUILD_DIR ?= build/release-build
CMAKE ?= cmake
PYTHON ?= python3
BUILD_TYPE ?= RelWithDebInfo
COMPILER_CACHE ?= ON
BUILD_JOBS ?= 4
TEST_JOBS ?= 2
TEST ?=
TEST_REPORT_DIR ?= $(abspath $(BUILD_DIR)/test-results)
SID_PREFIX ?= $(abspath $(BUILD_DIR)/deps/libresidfp)
SCENE ?= marshmallow

.DEFAULT_GOAL := help
.PHONY: help doctor audio-setup configure build run release play-late test check check-extended check-all smoke check-live-input check-standalone clean

help:
	@echo 'make doctor          Check tools and embedded assets'
	@echo 'make build           Build the game (optional ccache; COMPILER_CACHE=OFF disables it)'
	@echo 'make run             Build and start the game'
	@echo 'make release         Build release ZIPs for this system in build/release/'
	@echo 'make check           Run bounded quick tests and save reports'
	@echo 'make check TEST=zuul  Run only matching quick module/scene tests (name regex)'
	@echo 'make check-extended  Run four connected campaign/outcome checks explicitly'
	@echo 'make check-all       Include all vehicle/equipment/outcome variants (slow, opt-in)'
	@echo 'make smoke           Check startup without a display'
	@echo 'make play-late SCENE=marshmallow  Fast-forward to marshmallow, zuul, victory or defeat'
	@echo 'make check-live-input Check SDL keyboard input using private Xvfb'
	@echo 'make check-standalone Verify a fresh build and standalone executable'
	@echo 'make clean           Clean compiled outputs'

doctor:
	$(PYTHON) tools/doctor.py

audio-setup:
	$(PYTHON) tools/setup_audio.py --prefix "$(SID_PREFIX)"

configure: audio-setup
	$(CMAKE) -S . -B "$(BUILD_DIR)" -G Ninja -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DGHOSTBUSTERS_COMPILER_CACHE=$(COMPILER_CACHE) -DBUILD_TESTING=ON -DSID_PREFIX="$(SID_PREFIX)"

build: configure
	$(CMAKE) --build "$(BUILD_DIR)" --parallel $(BUILD_JOBS)

run: build
	"$(BUILD_DIR)/ghostbusters"

release: audio-setup
	$(CMAKE) -S . -B "$(RELEASE_BUILD_DIR)" -G Ninja -DCMAKE_BUILD_TYPE=Release -DGHOSTBUSTERS_COMPILER_CACHE=$(COMPILER_CACHE) -DBUILD_TESTING=OFF -DGHOSTBUSTERS_RELEASE=ON -DSID_PREFIX="$(SID_PREFIX)" $(if $(GHOSTBUSTERS_SDL_NOTICE),-DGHOSTBUSTERS_SDL_NOTICE="$(GHOSTBUSTERS_SDL_NOTICE)")
	$(CMAKE) --build "$(RELEASE_BUILD_DIR)" --target release --parallel $(BUILD_JOBS)

play-late: build
	$(PYTHON) tools/play_late_scene.py --build-dir "$(BUILD_DIR)" --scene "$(SCENE)"

test: build
	$(PYTHON) tools/run_tests.py --build-dir "$(BUILD_DIR)" --reports "$(TEST_REPORT_DIR)" --jobs $(TEST_JOBS) --suite quick $(if $(TEST),--match '$(TEST)')

check: test

check-extended check-all: build
	$(PYTHON) tools/run_tests.py --build-dir "$(BUILD_DIR)" --reports "$(TEST_REPORT_DIR)" --jobs $(TEST_JOBS) --suite $(if $(filter check-all,$@),all,extended) $(if $(TEST),--match '$(TEST)')

smoke: build
	SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software SDL_AUDIODRIVER=dummy "$(BUILD_DIR)/ghostbusters" --smoke-test

check-live-input: build
	$(PYTHON) tools/check_live_input.py --build-dir "$(BUILD_DIR)"
	$(PYTHON) tools/check_live_input.py --build-dir "$(BUILD_DIR)" --start-key F3

check-standalone:
	$(PYTHON) tools/check_standalone.py

clean:
	$(CMAKE) --build "$(BUILD_DIR)" --target clean
