DESTDIR=/
PREFIX=$(DESTDIR)/usr/local
DEPDIR=.deps
LIBS = $(shell pkg-config libcap_utils-0.7 libcap_filter-0.7 --libs) -lqd
bin_PROGRAMS = bitrate pktrate timescale wavelet
.PHONY: clean env-check

all: $(bin_PROGRAMS) env-check

bitrate: bitrate.o extract.o
	$(CXX) $(LDFLAGS) $^ $(LIBS) -o $@

pktrate: pktrate.o extract.o
	$(CXX) $(LDFLAGS) $^ $(LIBS) -o $@

timescale: timescale.o extract.o
	$(CXX) $(LDFLAGS) $^ $(LIBS) -o $@

wavelet: wavelet.o extract.o
	$(CXX) $(LDFLAGS) $^ $(LIBS) -o $@

env-check:
	@pkg-config libcap_utils-0.7 --atleast-version=0.7.14 || (echo "libcap_utils must be at least version 0.7.14, please update"; exit 1)

clean:
	rm -rf *.o $(bin_PROGRAMS) $(DEPDIR)

$(DEPDIR):
	mkdir -p $@

%.o: %.cpp Makefile $(DEPDIR)
	$(CXX) -Wall -std=c++0x -DHAVE_CONFIG_H $(CFLAGS) $(shell pkg-config libcap_utils-0.7 --cflags) -c $< -MD -MF $(DEPDIR)/$(@:.o=.d) -o $@

install: all
	install -m 0755 bitrate $(PREFIX)/bin
	install -m 0755 pktrate $(PREFIX)/bin
	install -m 0755 timescale $(PREFIX)/bin
	install -m 0755 wavelet $(PREFIX)/bin

-include $(wildcard $(DEPDIR)/*.d)
