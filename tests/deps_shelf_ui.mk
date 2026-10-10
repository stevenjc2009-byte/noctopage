DEPS_shelf_ui = ../src/ui/grid.c ../src/ui/menu.c
CFLAGS_shelf_ui = -Istubs -ffunction-sections -fdata-sections
LIBS_shelf_ui = -Wl,--gc-sections -lm
