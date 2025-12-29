#
# Ocean Userspace Build Rules
#

# Userspace directories
LIBC_DIR := lib/libc
LIBOCEAN_DIR := lib/libocean
SERVERS_DIR := servers
FS_DIR := fs
INCLUDE_DIR := include

# Userspace compiler flags
USER_CFLAGS := -std=gnu11 -g \
               -ffreestanding \
               -fno-stack-protector \
               -fno-pie \
               -fPIC \
               -Wall -Wextra \
               -I$(LIBC_DIR)/include \
               -I$(LIBOCEAN_DIR)/include \
               -I$(INCLUDE_DIR) \
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
INIT_OBJS := $(INIT_SRCS:$(SERVERS_DIR)/init/%.c=$(BUILD_DIR)/servers/init/%.o)

# Memory server
MEM_SRCS := $(wildcard $(SERVERS_DIR)/mem/*.c)
MEM_OBJS := $(MEM_SRCS:$(SERVERS_DIR)/mem/%.c=$(BUILD_DIR)/servers/mem/%.o)

# Process server
PROC_SRCS := $(wildcard $(SERVERS_DIR)/proc/*.c)
PROC_OBJS := $(PROC_SRCS:$(SERVERS_DIR)/proc/%.c=$(BUILD_DIR)/servers/proc/%.o)

# VFS server
VFS_SRCS := $(wildcard $(SERVERS_DIR)/vfs/*.c)
VFS_OBJS := $(VFS_SRCS:$(SERVERS_DIR)/vfs/%.c=$(BUILD_DIR)/servers/vfs/%.o)

# RAMFS driver
RAMFS_SRCS := $(wildcard $(FS_DIR)/ramfs/*.c)
RAMFS_OBJS := $(RAMFS_SRCS:$(FS_DIR)/ramfs/%.c=$(BUILD_DIR)/fs/ramfs/%.o)

# Userspace linker script
USER_LD_SCRIPT := user.ld

# All server binaries
SERVER_BINS := $(BUILD_DIR)/init.elf \
               $(BUILD_DIR)/mem.elf \
               $(BUILD_DIR)/proc.elf \
               $(BUILD_DIR)/vfs.elf \
               $(BUILD_DIR)/ramfs.elf

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
$(BUILD_DIR)/servers/init/%.o: $(SERVERS_DIR)/init/%.c
	@echo "  CC [init] $<"
	@mkdir -p $(dir $@)
	@$(CC) $(USER_CFLAGS) -c $< -o $@

# Build memory server
$(BUILD_DIR)/servers/mem/%.o: $(SERVERS_DIR)/mem/%.c
	@echo "  CC [mem] $<"
	@mkdir -p $(dir $@)
	@$(CC) $(USER_CFLAGS) -c $< -o $@

# Build process server
$(BUILD_DIR)/servers/proc/%.o: $(SERVERS_DIR)/proc/%.c
	@echo "  CC [proc] $<"
	@mkdir -p $(dir $@)
	@$(CC) $(USER_CFLAGS) -c $< -o $@

# Build VFS server
$(BUILD_DIR)/servers/vfs/%.o: $(SERVERS_DIR)/vfs/%.c
	@echo "  CC [vfs] $<"
	@mkdir -p $(dir $@)
	@$(CC) $(USER_CFLAGS) -c $< -o $@

# Build RAMFS driver
$(BUILD_DIR)/fs/ramfs/%.o: $(FS_DIR)/ramfs/%.c
	@echo "  CC [ramfs] $<"
	@mkdir -p $(dir $@)
	@$(CC) $(USER_CFLAGS) -c $< -o $@

# Helper function to link a userspace binary
define link_user_binary
	@echo "  LD [user] $@"
	@mkdir -p $(dir $@)
	@$(LD) $(USER_LDFLAGS) -T $(USER_LD_SCRIPT) -o $@ $(BUILD_DIR)/libc/crt0.o $(1) $(filter-out $(BUILD_DIR)/libc/crt0.o,$(LIBC_OBJS))
endef

# Link init binary
$(BUILD_DIR)/init.elf: $(INIT_OBJS) $(LIBC_OBJS) $(USER_LD_SCRIPT)
	$(call link_user_binary,$(INIT_OBJS))

# Link memory server
$(BUILD_DIR)/mem.elf: $(MEM_OBJS) $(LIBC_OBJS) $(USER_LD_SCRIPT)
	$(call link_user_binary,$(MEM_OBJS))

# Link process server
$(BUILD_DIR)/proc.elf: $(PROC_OBJS) $(LIBC_OBJS) $(USER_LD_SCRIPT)
	$(call link_user_binary,$(PROC_OBJS))

# Link VFS server
$(BUILD_DIR)/vfs.elf: $(VFS_OBJS) $(LIBC_OBJS) $(USER_LD_SCRIPT)
	$(call link_user_binary,$(VFS_OBJS))

# Link RAMFS driver
$(BUILD_DIR)/ramfs.elf: $(RAMFS_OBJS) $(LIBC_OBJS) $(USER_LD_SCRIPT)
	$(call link_user_binary,$(RAMFS_OBJS))

# Phony targets
.PHONY: userspace
userspace: $(SERVER_BINS)

.PHONY: servers
servers: $(SERVER_BINS)

.PHONY: clean-user
clean-user:
	@rm -rf $(BUILD_DIR)/libc $(BUILD_DIR)/servers $(BUILD_DIR)/fs $(SERVER_BINS)
