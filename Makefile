TARGET_DIR  := $(CURDIR)/build
INCLUDE_DIR := $(CURDIR)/include
SRC_DIR     := $(CURDIR)/src
OBJ_DIR     := $(TARGET_DIR)/obj

CROSS_COMPILE := riscv64-unknown-elf-

SRC         := $(wildcard $(SRC_DIR)/*.c)
OBJ         := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRC))

CC          := gcc
CFLAGS      := -I$(INCLUDE_DIR) -O3

VERILATOR_VSRC_DIR	:=	$(CURDIR)/verilog
VERILATOR_CSRC_DIR	:=	$(CURDIR)/src
VERILATOR_DEST_DIR	:=	$(TARGET_DIR)/verilator
VERILATOR_CXXFLAGS	:=	-O3 -std=c++11 -fpermissive -g -I$(VERILATOR_CSRC_DIR) -I$(VERILATOR_DEST_DIR)/build -I$(INCLUDE_DIR)
VERILATOR_LDFLAGS 	:=	-Wl,--export-dynamic -lpthread -ldl
VERILATOR_SOURCE 	:= $(sort $(wildcard $(VERILATOR_CSRC_DIR)/*.cpp)) $(sort $(wildcard $(VERILATOR_CSRC_DIR)/*.c))

VERILATOR_FLAGS := --cc --exe --top-module TileForVerilator	\
				  --threads 1 \
				  --assert --x-assign unique    \
				  --output-split 20000 -O3    	\
				  -I$(VERILATOR_VSRC_DIR) 	  	\
				  -CFLAGS "$(VERILATOR_CXXFLAGS)" \
				  -LDFLAGS "$(VERILATOR_LDFLAGS)"

RV_TESTS_DIR	:= $(CURDIR)/riscv-tests

all: $(TARGET_DIR)/emulator

$(TARGET_DIR)/emulator: $(TARGET_DIR)/TileForVerilator.v
	mkdir -p $(VERILATOR_DEST_DIR)
	verilator $(VERILATOR_FLAGS) -o $(TARGET_DIR)/emulator -Mdir $(VERILATOR_DEST_DIR)/build $(TARGET_DIR)/TileForVerilator.v $(VERILATOR_SOURCE)
	$(MAKE) -C $(VERILATOR_DEST_DIR)/build -f $(VERILATOR_DEST_DIR)/build/VTileForVerilator.mk

prepare:
	mkdir -p build/cases
	cd $(RV_TESTS_DIR) && autoconf && ./configure --prefix=/opt
	cd $(RV_TESTS_DIR) && $(MAKE)
	cp -v $(RV_TESTS_DIR)/isa/$(ELF) $(TARGET_DIR)/cases
	cp -v $(RV_TESTS_DIR)/isa/$(ELF).dump $(TARGET_DIR)/cases
	$(CROSS_COMPILE)objcopy -O binary $(TARGET_DIR)/cases/$(ELF) $(TARGET_DIR)/cases/$(ELF).bin
	od -t x4 -An -w4 -v $(TARGET_DIR)/cases/$(ELF).bin > $(TARGET_DIR)/cases/$(ELF).hex
	cp -v $(VERILATOR_VSRC_DIR)/TileForVerilator.v $(TARGET_DIR)/TileForVerilator.v
	sed -i 's/TESTFILE/cases\/$(ELF).hex/g' $(TARGET_DIR)/TileForVerilator.v

clean:
	-@rm -rf $(TARGET_DIR)
	-@rm $(UCORE_PREFIX).hex $(UCORE_PREFIX).bin
