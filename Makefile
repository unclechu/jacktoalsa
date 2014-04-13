NAME = jacktoalsa
INSTALL_PREFIX = /usr/local
LIBS = `pkg-config --cflags --libs jack alsa` -lm

$(NAME): clean bin
	mkdir build
	mv $(NAME) build/$(NAME)

bin:
	gcc src/$(NAME).c $(LIBS) -o $(NAME)

clean:
	rm -rf build $(NAME)

install: $(NAME)
	mkdir -p "$(INSTALL_PREFIX)/bin"
	cp -f build/$(NAME) "$(INSTALL_PREFIX)/bin"
