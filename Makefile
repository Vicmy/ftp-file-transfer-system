# ===================== 编译器与编译选项配置 =====================
# 指定编译器为 gcc
CC = gcc

# 编译参数：
# -Wall    : 打开所有警告
# -g       : 生成调试信息（方便GDB调试）
# -D_FILE_OFFSET_BITS=64 : 支持大于4GB的大文件（解决2GB文件大小限制）

# 链接参数：
# -lpthread: 链接 pthread 库（处理多线程）
CFLAGS = -Wall -g -D_FILE_OFFSET_BITS=64
LDFLAGS = -lpthread


# ===================== 目录与目标文件配置 =====================
# 二进制可执行文件存放目录
BINDIR = bin

# 最终生成的服务端程序路径
TARGET_SERVER = $(BINDIR)/server
# 最终生成的客户端程序路径
TARGET_CLIENT = $(BINDIR)/client

# ===================== 源文件配置 =====================
# 服务端依赖的所有源文件
SERVER_SRC = server/server.c common/utils.c common/log.c
# 客户端依赖的所有源文件
CLIENT_SRC = client/client.c common/utils.c

# ===================== 核心编译规则 =====================
# 默认目标（输入 make 时执行）
# 依赖：创建bin目录、编译服务端、编译客户端
all: $(BINDIR) $(TARGET_SERVER) $(TARGET_CLIENT)

# 创建二进制目录（如果不存在）
$(BINDIR):
	mkdir -p $(BINDIR)

# 生成服务端可执行文件
# 依赖：服务端所有源文件 | 确保目录已创建
$(TARGET_SERVER): $(SERVER_SRC) | $(BINDIR)
	# 执行编译命令：$@ 代表目标文件 $(TARGET_SERVER)
	$(CC) $(CFLAGS) -o $@ $(SERVER_SRC) $(LDFLAGS)

# 生成客户端可执行文件
# 依赖：客户端所有源文件 | 确保目录已创建
$(TARGET_CLIENT): $(CLIENT_SRC) | $(BINDIR)
	# 执行编译命令
	$(CC) $(CFLAGS) -o $@ $(CLIENT_SRC)

# ===================== 清理规则 =====================
# 清理命令：删除整个 bin 目录
clean:
	rm -rf $(BINDIR)

# ===================== 伪目标声明 =====================
# 声明 all 和 clean 是伪目标（不是文件名）
.PHONY: all clean