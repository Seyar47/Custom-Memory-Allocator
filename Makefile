CC = gcc

CFLAGS = -Wall -Wextra -pthread -g 

TARGET = allocator_test		
SRCS = main.c allocator.c debug.c 	

OBJS = $(SRCS:.c=.o)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) 

%.o: %.c allocator.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
run: $(TARGET)
	./$(TARGET)	




