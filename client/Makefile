# #可执行文件
TARGET = ./bin/ImageSend
CXXFLAGS :=  -w -I./inc
LDFLAGS := -lpthread -lcrypto

# 定义编译器
CXX = g++ -std=c++11 -g

# 定义源文件和目标文件
SRC = main.cpp 

# 添加其他目录中的源文件
# SRC += $(wildcard src/*.cpp )
SRC += $(shell find . -name "*.cpp")
# 默认目标
all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) -o $@ $^ $(CXXFLAGS) $(LDFLAGS)

# 清理生成的文件
clean:
	rm -f  $(TARGET) 

.PHONY: all clean


                                                                 

