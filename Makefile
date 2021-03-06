include config.mk

NAME_DEFINES = -D__NAME__=\"$(__NAME__)\"                 \
			   -D__NAME_CLIENT__=\"$(__NAME_CLIENT__)\"   \
			   -D__THIS_VERSION__=\"$(__THIS_VERSION__)\" \
			   -D__CONFIG_NAME__=\"$(__CONFIG_NAME__)\"   \

SRC = src/wm.cpp src/client.cpp src/common.cpp src/xcb.cpp src/ipc/server.cpp
OBJ = $(SRC:.cpp=.o)
BIN = $(__NAME__) $(__NAME_CLIENT__)
CXXFLAGS += $(NAME_DEFINES)
CXXFLAGS += -std=c++17 -stdlib=libc++

all: $(BIN)

debug: CXXFLAGS += -O0 -g -DD
debug: $(__NAME__) $(__NAME_CLIENT__)

$(__NAME__): src/wm.o src/xcb.o src/ipc/server.o src/common.o
	@echo $@
	@$(CXX) -o $@ $^ $(CXXFLAGS) $(LDFLAGS)

$(__NAME_CLIENT__): src/client.o src/common.o
	@echo $@
	@$(CXX) -o $@ $^ $(CXXFLAGS) $(LDFLAGS)

%.o: %.c
	@echo $@
	@$(CC) -o $@ -c $(CFLAGS) $<

%.o: %.cpp
	@echo $@
	@$(CXX) -x c++ -o $@ -c $(CXXFLAGS) $<

$(OBJ): src/common.hpp src/client.hpp src/config.hpp src/wm.hpp src/util.hpp src/types.hpp src/xcb.hpp src/ipc/commands.hpp src/ipc/handlers.hpp src/ipc/parsers.hpp src/ipc/server.hpp

install: all
	mkdir -p "$(DESTDIR)$(PREFIX)/bin"
	install $(__NAME__) "$(DESTDIR)$(PREFIX)/bin/$(__NAME__)"
	install $(__NAME_CLIENT__) "$(DESTDIR)$(PREFIX)/bin/$(__NAME_CLIENT__)"
	mkdir -p "$(DESTDIR)$(DOCPREFIX)/$(__NAME__)/"
	cp -fR contrib "$(DESTDIR)$(DOCPREFIX)/$(__NAME__)/"
	cp -fR examples "$(DESTDIR)$(DOCPREFIX)/$(__NAME__)/"
	cp -f README.md LICENSE "$(DESTDIR)$(DOCPREFIX)/$(__NAME__)/"
	cd ./man; $(MAKE) install

uninstall:
	rm -f "$(DESTDIR)$(PREFIX)/bin/$(__NAME__)"
	rm -f "$(DESTDIR)$(PREFIX)/bin/$(__NAME_CLIENT__)"
	rm -rf "$(DESTDIR)$(DOCPREFIX)/$(__NAME__)"
	cd ./man; $(MAKE) uninstall

clean:
	rm -f $(OBJ) $(BIN)
