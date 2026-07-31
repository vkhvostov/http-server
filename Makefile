NAME        = http-server
CXX         = c++
CXXFLAGS    = -Wall -Wextra -Werror -std=c++98
RM          = rm -rf

# --- Directories ---
OBJ_DIR     = obj
INC_DIR     = includes
SRC_DIR     = src

# --- Operating System Detection ---
UNAME_S := $(shell uname -s)

# --- Source Files ---
# Common files
SRCS_COMMON = main.cpp

# Network / Core
SRCS_NETWORK = network/Socket.cpp \
               network/Config.cpp \
               network/EventLoop.cpp

# HTTP / CGI Logic
SRCS_HTTP    = http/Request.cpp \
               http/RequestHandler.cpp \
               http/Response.cpp \
               http/CGI.cpp

# --- OS Specific Files & Flags ---
ifeq ($(UNAME_S), Linux)
    SRCS_NETWORK += network/EpollHandler.cpp
    CXXFLAGS     += -D OS_LINUX
    MESSAGE      = "Compiling for Linux (Epoll)..."
endif

ifeq ($(UNAME_S), Darwin)
    SRCS_NETWORK += network/KqueueHandler.cpp
    CXXFLAGS     += -D OS_MAC
    MESSAGE      = "Compiling for macOS (Kqueue)..."
endif

# --- Combine all sources and objects ---
ALL_SRCS    = $(SRCS_COMMON) $(SRCS_NETWORK) $(SRCS_HTTP)
OBJS        = $(addprefix $(OBJ_DIR)/, $(ALL_SRCS:.cpp=.o))

# --- Headers inclusion ---
INCLUDES    = -I $(INC_DIR) -I $(INC_DIR)/network -I $(INC_DIR)/http

# ==============================================================================
# RULES
# ==============================================================================

all: $(NAME)

$(NAME): $(OBJS)
	@echo "\033[32m"
	@echo "Linking $(NAME)..."
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(NAME)
	@echo "Done!"
	@echo "\033[0m"

# Rule for object files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	@echo "Compiling $<..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

clean:
	@echo "\033[33mCleaning objects...\033[0m"
	$(RM) $(OBJ_DIR)

fclean: clean
	@echo "\033[31mRemoving binary...\033[0m"
	$(RM) $(NAME)

re: fclean all

# Progress message
info:
	@echo "\033[36m"
	@echo "OS detected: $(UNAME_S)"
	@echo "Flags: $(CXXFLAGS)"
	@echo "\033[0m"

.PHONY: all clean fclean re info