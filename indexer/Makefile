DESTDIR=/var/pcapdb

build-capture:
	echo ${DESTDIR}
	mkdir -p build
	pushd build; cmake ../
	#-DCMAKE_INSTALL_PREFIX=${DESTDIR} ../
	make -C build

install: install_scripts
	pushd build; cmake ../
	#-DCMAKE_INSTALL_PREFIX=${DESTDIR} ../
	make -C build install

install_scripts:
	install scripts/* ${DESTDIR}/bin/

clean:
	rm -rf build
