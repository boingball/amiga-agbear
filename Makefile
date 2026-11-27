# AG Bear - AG Bear Pet and AG Bear Talker for AmigaOS 3.x (AGA or RTG)
#
#   make            both programs (build/AGBearPet, build/AGBearTalker)
#   make release    release/AGBear drawer with icons and ReadMe
#   make assets     re-generate art/ and src/gen/ from the pictures and sounds (needs Python 3 + Pillow + numpy)
#   make icons      re-generate the Workbench icons in icons/
#
# The generated sources in src/gen/ are committed, so a plain build needs only the cross compiler.

CROSS_CC ?= m68k-amigaos-gcc
PYTHON ?= python3
CFLAGS ?= -O2 -m68020-60 -Wall -Wno-pointer-sign -fomit-frame-pointer
LDFLAGS ?= -noixemul -s

COMMON := src/common/agb_gfx.c src/common/agb_audio.c src/gen/sounds_data.c
COMMON_H := src/common/agb_gfx.h src/common/agb_audio.h src/gen/sounds_data.h
PET_SRC := src/pet/agbearpet.c src/gen/pet_data.c
TALK_SRC := src/talker/agbeartalker.c src/gen/talk_data.c
REL := release/AGBear

.PHONY: all release assets icons clean

all: build/AGBearPet build/AGBearTalker

build/AGBearPet: $(PET_SRC) $(COMMON) $(COMMON_H) src/gen/pet_data.h
	mkdir -p build
	$(CROSS_CC) $(CFLAGS) -o $@ $(PET_SRC) $(COMMON) $(LDFLAGS)

build/AGBearTalker: $(TALK_SRC) $(COMMON) $(COMMON_H) src/gen/talk_data.h
	mkdir -p build
	$(CROSS_CC) $(CFLAGS) -o $@ $(TALK_SRC) $(COMMON) $(LDFLAGS)

assets:
	$(PYTHON) tools/mkart.py
	$(PYTHON) tools/mkassets.py

icons:
	$(PYTHON) tools/wbicons.py --write wbicons

release: all
	rm -rf $(REL)
	mkdir -p $(REL)
	cp build/AGBearPet build/AGBearTalker $(REL)/
	cp wbicons/AGBearPet.info wbicons/AGBearTalker.info $(REL)/
	cp docs/ReadMe.txt $(REL)/ReadMe
	cp wbicons/ReadMe.info $(REL)/ReadMe.info
	cp wbicons/AGBear-drawer.info release/AGBear.info

clean:
	rm -rf build release
