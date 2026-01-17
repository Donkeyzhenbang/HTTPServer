# #可执行文件
TARGET = ./bin/SocketServer
CXXFLAGS :=  -w -I./inc
LDFLAGS := -lpthread -lcrypto

# 定义编译器
CXX = g++ -std=c++17 -g

# 定义源文件和目标文件
# SRC = main.cpp 

# 添加其他目录中的源文件
# SRC = $(wildcard src/*.cpp )
SRC = $(shell find . -name "*.cpp")
OBJ = $(SRC:.cpp=.o)
# 默认目标
all: $(TARGET) 

# 生成目标文件（.o 文件）
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# 生成可执行文件
$(TARGET): $(OBJ)
	$(CXX) -o $@ $^ $(LDFLAGS) -O2
# 清理生成的文件
clean:
	rm -f  $(TARGET) $(OBJ)

.PHONY: all clean


                                                                 

