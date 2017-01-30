DESTDIR=/var/pcapdb

build:
	mkdir -p build
	cd build; cmake -DCMAKE_INSTALL_PREFIX="" ../
	make -C build

install: install_scripts build
	make -C build install 

install_scripts:
	install scripts/* ${DESTDIR}/bin/

clean:
	rm -rf build
