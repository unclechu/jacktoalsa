NAME = jacktoalsa
LIBS = `pkg-config --cflags --libs jack alsa`

meta-$(NAME): clean bin
	mkdir build
	mv meta-$(NAME) build/meta-$(NAME)

bin:
	gcc src/$(NAME).c $(LIBS) -o meta-$(NAME)

clean:
	rm -rf build $(NAME)
