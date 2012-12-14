COMPILER=gcc
CLASSES_TO_COMPILE=s_string.c poll_t.c
GL_CLASSES_TO_COMPILE=screen.c
LIBS=curl json
GL_LIBS=X11 GL m curl
EXT=.exe

all:
	$(COMPILER) -c $(CLASSES_TO_COMPILE) $(LIBS:%=-l%)

glScreen: all
	$(COMPILER) $(GL_CLASSES_TO_COMPILE) $(CLASSES_TO_COMPILE:%.c=%.o) $(LIBS:%=-l%) $(GL_LIBS:%=-l%) -o $@$(EXT)

test: all
	$(COMPILER) test.c $(CLASSES_TO_COMPILE:%.c=%.o) $(LIBS:%=-l%) -o $@$(EXT)

