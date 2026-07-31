.PHONY: all build clean rebuild help

BUILD_DIR := build
CMAKE := cmake
CMAKE_FLAGS := -DCMAKE_C_COMPILER=/usr/bin/gcc -DCMAKE_CXX_COMPILER=/usr/bin/g++

all: build

build: $(BUILD_DIR)/Makefile
	$(MAKE) -C $(BUILD_DIR)

$(BUILD_DIR)/Makefile: CMakeLists.txt
	@mkdir -p $(BUILD_DIR)
	cd $(BUILD_DIR) && $(CMAKE) $(CMAKE_FLAGS) ..

clean:
	rm -rf $(BUILD_DIR)

rebuild: clean build

help:
	@echo "ppstep - C preprocessor macro-expansion debugger"
	@echo ""
	@echo "Targets:"
	@echo "  make          Build ppstep (default)"
	@echo "  make build    Build ppstep"
	@echo "  make clean    Remove build directory"
	@echo "  make rebuild  Clean rebuild"
	@echo "  make help     Show this help"
	@echo ""
	@echo "Usage after build:"
	@echo "  ./build/ppstep [options] <input-file>"
	@echo ""
	@echo "Options:"
	@echo "  -h, --help          Show ppstep options"
	@echo "  -I, --include PATH  Add include path"
	@echo "  -D, --define MACRO  Define macro (as NAME[=[value]])"
	@echo "  -U, --undefine MACRO Undefine macro"
	@echo "  --debug             Enable debug trace mode"
	@echo ""
	@echo "Interactive commands (type 'help' in the debugger for full list):"
	@echo "  step [N] / s [N]    Step N preprocessing events"
	@echo "  continue / c        Continue until breakpoint or end"
	@echo "  backtrace [N] / bt  Show expansion backtrace"
	@echo "  break call MACRO    Set breakpoint on macro call"
	@echo "  info breakpoints    List breakpoints"
	@echo "  info macros         List defined macros"
	@echo "  expand MACRO        Expand a macro inline"
	@echo "  quit / q            Exit debugger"
