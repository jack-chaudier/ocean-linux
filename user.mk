#
# Ocean Userspace Build Rules
#

# Userspace directories
LIBC_DIR := lib/libc
LIBOCEAN_DIR := lib/libocean
SERVERS_DIR := servers

# Userspace compiler flags
USER_CFLAGS := -std=gnu11 -g \
               -ffreestanding \
               -fno-stack-protector \
               -fno-pie \
               -fPIC \
               -Wall -Wextra \
               -I$(LIBC_DIR)/include \
               -I$(LIBOCEAN_DIR)/include \
               -nostdinc

USER_LDFLAGS := -nostdlib \
                -static \
                -z max-page-size=0x1000

# Userspace sources
LIBC_SRCS := $(wildcard $(LIBC_DIR)/src/*.c)
LIBC_ASM_SRCS := $(wildcard $(LIBC_DIR)/src/*.S)
LIBC_OBJS := $(LIBC_SRCS:$(LIBC_DIR)/src/%.c=$(BUILD_DIR)/libc/%.o) \
             $(LIBC_ASM_SRCS:$(LIBC_DIR)/src/%.S=$(BUILD_DIR)/libc/%.o)

# Init server
INIT_SRCS := $(wildcard $(SERVERS_DIR)/init/*.c)
INIT_OBJS := $(INIT_SRCS:$(SERVERS_DIR)/init/%.c=$(BUILD_DIR)/init/%.o)

# Userspace linker script
USER_LD_SCRIPT := user.ld

# Build libc objects
$(BUILD_DIR)/libc/%.o: $(LIBC_DIR)/src/%.c
	@echo "  CC [user] $<"
	@mkdir -p $(dir $@)
	@$(CC) $(USER_CFLAGS) -c $< -o $@

$(BUILD_DIR)/libc/%.o: $(LIBC_DIR)/src/%.S
	@echo "  AS [user] $<"
	@mkdir -p $(dir $@)
	@$(CC) $(USER_CFLAGS) -c $< -o $@

# Build init server
$(BUILD_DIR)/init/%.o: $(SERVERS_DIR)/init/%.c
	@echo "  CC [init] $<"
	@mkdir -p $(dir $@)
	@$(CC) $(USER_CFLAGS) -c $< -o $@

# Link init binary
$(BUILD_DIR)/init.elf: $(INIT_OBJS) $(LIBC_OBJS) $(USER_LD_SCRIPT)
	@echo "  LD [user] $@"
	@mkdir -p $(dir $@)
	@$(LD) $(USER_LDFLAGS) -T $(USER_LD_SCRIPT) -o $@ $(BUILD_DIR)/libc/crt0.o $(INIT_OBJS) $(filter-out $(BUILD_DIR)/libc/crt0.o,$(LIBC_OBJS))

# Phony targets
.PHONY: userspace
userspace: $(BUILD_DIR)/init.elf

.PHONY: clean-user
clean-user:
	@rm -rf $(BUILD_DIR)/libc $(BUILD_DIR)/init $(BUILD_DIR)/init.elf
