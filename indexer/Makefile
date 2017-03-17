DESTDIR=/var/pcapdb

build/Makefile:
	mkdir -p build
	cd build; cmake -DCMAKE_INSTALL_PREFIX="" ../
	make -C build

install: install_scripts build/Makefile
	make -C build install 

install_scripts:
	install scripts/* ${DESTDIR}/bin/

clean:
	rm -rf build
