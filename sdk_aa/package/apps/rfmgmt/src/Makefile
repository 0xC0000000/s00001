LINK =-lpthread -lrt -ldl -luci

OBJS = rfmgmt_main.o rfmgmt_config.o rfmgmt_data_receiver.o \
	   rfmgmt_data_recorder.o rfmgmt_data_uploader.o \
	   utils.o db_process.o circular_queue.o sqlite3.o led_mgr.o

EPC_OBJ = getepc.o db_process.o utils.o sqlite3.o rfmgmt_config.o

.PHONY: all
all: rfmgmt getepc

rfmgmt: $(OBJS)
	$(CC) $(LDFLAGS) $(LINK) -o rfmgmt $(OBJS)

getepc: $(EPC_OBJ)
	$(CC) $(LDFLAGS) $(LINK) -o getepc $(EPC_OBJ)

%.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@

.PHONY: clean
clean:
	rm -f $(OBJS) rfmgmt getepc


