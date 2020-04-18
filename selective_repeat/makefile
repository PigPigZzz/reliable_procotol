CC=gcc
SRCS=$(wildcard *.c */*.c)
OBJS=$(patsubst %.c, %.o, $(SRCS))
FLAG=-g -lpthread
NAME=$(wildcard *.c)
TARGET=$(patsubst %.c, %, $(NAME))

$(TARGET):$(OBJS)
	$(CC) -o $@ $^ $(FLAG)

%.o:%.c
	$(CC) -o $@ -c $< -g

clean:
	rm -rf $(TARGET) $(OBJS)

