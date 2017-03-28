DESTDIR=/var/pcapdb

BUILD: build/Makefile
	make -C build

build/Makefile: 
	mkdir -p build
	cd build; cmake -DCMAKE_INSTALL_PREFIX="" ../

install: install_scripts BUILD
	make -C build install 

install_scripts:
	install scripts/* ${DESTDIR}/bin/

clean:
	rm -rf build
