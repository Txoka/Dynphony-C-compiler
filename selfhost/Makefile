PYTHON ?= python
BUILD_DIR ?= build

.PHONY: all stages stage0 stage1-stage2 demo pack test clean

all: stages

stages: stage0 stage1-stage2

stage0:
	mkdir -p $(BUILD_DIR)
	cd .. && $(PYTHON) -m symphony \
		selfhost/src/main.c selfhost/src/compiler.c \
		selfhost/src/frontend/lexer.c selfhost/src/frontend/preprocessor.c \
		selfhost/src/frontend/parser.c \
		selfhost/src/frontend/sema.c selfhost/src/middle/lower.c \
		selfhost/src/middle/optimize.c \
		selfhost/src/target/symphony/backend.c \
		-I selfhost/include -o selfhost/$(BUILD_DIR)/scc-stage0.bin

stage1-stage2: stage0
	cd .. && $(PYTHON) selfhost/tools/build_stages.py \
		--stage0 selfhost/$(BUILD_DIR)/scc-stage0.bin \
		--output-dir selfhost/$(BUILD_DIR) --load-address 0

demo:
	cd .. && $(PYTHON) selfhost/tools/bootstrap.py

pack:
	cd .. && $(PYTHON) selfhost/tools/pack_project.py selfhost \
		--exclude examples -o selfhost/$(BUILD_DIR)/selfhost.pstore

test:
	cd .. && SYMPHONY_TEST_ISA=dynphony $(PYTHON) -m unittest discover -s selfhost/tests -v
	cd .. && SYMPHONY_TEST_ISA=symphony $(PYTHON) -m unittest discover -s selfhost/tests -v

clean:
	rm -rf $(BUILD_DIR)
