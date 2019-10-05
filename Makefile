PKGNAME = $(shell dpkg-parsechangelog -S source)
VERSION = $(shell dpkg-parsechangelog -S version | sed 's/-.*//')

all:

release:
	tar --numeric-owner --group 0 --owner 0 -cJh \
	  --xform "s,^,$(PKGNAME)-$(VERSION)/," \
	  -f $(PKGNAME)-$(VERSION).tar.xz \
	  line_protocol_parser/*.py include/*.h src/*.c tests/*.py setup.py \
	  README.rst

deb: release
	rm -rf $(PKGNAME)-$(VERSION)
	tar -xJf $(PKGNAME)-$(VERSION).tar.xz
	ln -sf $(PKGNAME)-$(VERSION).tar.xz \
	  $(PKGNAME)_$(VERSION).orig.tar.xz
	cp -a debian/ $(PKGNAME)-$(VERSION)/
	(cd $(PKGNAME)-$(VERSION) && dpkg-buildpackage -us -uc)

clean:
	-rm *.deb *.changes *.buildinfo *.tar.xz
	-rm -rf $(PKGNAME)-*
	-rm -rf dist/
	-rm -rf build/
	-rm -rf *.egg-info
	-rm line_protocol_parser/*.so

sdist:
	python setup.py sdist

wheel:
	python setup.py bdist_wheel
