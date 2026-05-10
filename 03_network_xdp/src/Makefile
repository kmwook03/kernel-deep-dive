CLANG ?= clang
LLC ?= llc

TARGET = xdp_drop_port.o

all: $(TARGET)

$(TARGET): xdp_drop_port.c
	$(CLANG) -O2 -g -Wall -target bpf -c $< -o $@

clean:
	rm -f $(TARGET)
