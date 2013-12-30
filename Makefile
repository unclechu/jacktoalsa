NAME = jacktoalsa
INSTALL_PREFIX = /usr/local
LIBS = `pkg-config --cflags --libs jack alsa` -lm

meta-$(NAME): clean bin
	mkdir build
	mv meta-$(NAME) build/meta-$(NAME)

bin:
	gcc src/$(NAME).c $(LIBS) -o meta-$(NAME)

clean:
	rm -rf build $(NAME)

install: meta-$(NAME)
	mkdir -p "$(INSTALL_PREFIX)/bin"
	cp -f build/meta-$(NAME) "$(INSTALL_PREFIX)/bin"
