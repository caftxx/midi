# 定义编译器和链接器
CC = gcc
CFLAGS = -Wall -g -lm

# 定义目标文件
TARGET = main convert

# 定义对象文件
OBJS = main.o convert.o

# 默认目标
all: $(TARGET)

# 模式规则：从.c文件生成.o文件
%.o: %.c midi.c
	$(CC) $(CFLAGS) -c $< -o $@

# 链接目标文件生成可执行文件
main: main.o midi.o
	$(CC) $^ -o $@ $(CFLAGS)

convert: convert.o midi.o
	$(CC) $^ -o $@ $(CFLAGS)

# 清理目标
clean:
	rm -f $(OBJS) $(TARGET)

# 包含依赖文件
-include $(OBJS:.o=.d)