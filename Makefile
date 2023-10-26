
CC		?= gcc
CFLAGS		+= -O0 -g
CPPFLAGS	+=

LD		= $(CC)
LDFLAGS		+=
LIBS		+=

##

OBJECTS		= jki_to_jik.o

TARGET		= jki_to_jik

##

default: $(TARGET)

clean::
	$(RM) $(OBJECTS)

##

$(TARGET): $(OBJECTS)
	$(LD) -o $@ $(LDFLAGS) $+

%.o: %.c
	$(CC) -c -o $@ $(CPPFLAGS) $< $(CFLAGS)

