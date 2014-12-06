
PREFIX=/usr
UDEVLIBDIR=/lib/udev
DRACUTLIBDIR=/lib/dracut
INSTALL=install
CFLAGS+=-O2 -Wall -Werror -g -I.

all: bcacheadm make-bcache probe-bcache bcachectl

install: bcacheadm make-bcache probe-bcache
	$(INSTALL) -m0755 bcacheadm bcachectl $(DESTDIR)${PREFIX}/sbin/
	$(INSTALL) -m0755 make-bcache bcachectl $(DESTDIR)${PREFIX}/sbin/
	$(INSTALL) -m0755 probe-bcache bcache-register		$(DESTDIR)$(UDEVLIBDIR)/
	$(INSTALL) -m0644 69-bcache.rules	$(DESTDIR)$(UDEVLIBDIR)/rules.d/
	#-$(INSTALL) -T -m0755 initramfs/hook	$(DESTDIR)/usr/share/initramfs-tools/hooks/bcache
	if [ -d $(DESTDIR)$(DRACUTLIBDIR)/modules.d ]; \
	then $(INSTALL) -D -m0755 dracut/module-setup.sh $(DESTDIR)$(DRACUTLIBDIR)/modules.d/90bcache/module-setup.sh; \
	fi
	$(INSTALL) -m0644 -- *.8 $(DESTDIR)${PREFIX}/share/man/man8/
#	$(INSTALL) -m0755 bcache-test $(DESTDIR)${PREFIX}/sbin/

clean:
	$(RM) -f make-bcache probe-bcache bcache-super-show bcache-test bcachectl *.o

bcache-test: LDLIBS += `pkg-config --libs openssl`

bcacheadm: LDLIBS += `pkg-config --libs uuid blkid libnih`
bcacheadm: CFLAGS += `pkg-config --cflags uuid blkid libnih`
bcacheadm: bcache.o

make-bcache: LDLIBS += `pkg-config --libs uuid blkid`
make-bcache: CFLAGS += `pkg-config --cflags uuid blkid`
make-bcache: bcache.o

probe-bcache: LDLIBS += `pkg-config --libs uuid blkid`
probe-bcache: CFLAGS += `pkg-config --cflags uuid blkid`

bcache-super-show: LDLIBS += `pkg-config --libs uuid`
bcache-super-show: CFLAGS += -std=gnu99
bcache-super-show: bcache.o

bcachectl: bcachectl.o
