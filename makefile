.PHONY: test clean install

ifeq ($(PREFIX_DIR),)
PREFIX_DIR:=$(HOME)/.local
endif

ochi: ochi.c
	gcc -o$@ $< -lc41 -lhbs1clid -lhbs1 -lacx1

test: ochi
	./ochi ochi

clean:
	rm ochi

install: ochi
	mkdir -p $(PREFIX_DIR)/bin
	cp -v ochi $(PREFIX_DIR)/bin

