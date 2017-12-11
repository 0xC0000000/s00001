OBJS = rtci.o

APP = rtci

.PHONY: all
all: $(APP)

rtci: $(OBJS)
	$(CC) -o $@ $(OBJS)

%.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@

.PHONY: clean
clean:
	rm $(APP) *.o *.~