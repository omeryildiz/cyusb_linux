all:
	g++ -o 00_fwload            00_fwload.cpp            -L ../lib -l cyusb
	g++ -o 01_getdesc           01_getdesc.cpp           -L ../lib -l cyusb
	g++ -o 03_getconfig         03_getconfig.cpp         -L ../lib -l cyusb
	g++ -o 04_kerneldriver      04_kerneldriver.cpp      -L ../lib -l cyusb
	g++ -o 05_claiminterface    05_claiminterface.cpp    -L ../lib -l cyusb
	g++ -o 06_setalternate      06_setalternate.cpp      -L ../lib -l cyusb
	g++ -o 08_cybulk            08_cybulk.cpp            -L ../lib -l cyusb -l pthread
	g++ -o 09_cyusb_performance 09_cyusb_performance.cpp -L ../lib -l cyusb -l usb-1.0
	g++ -o download_fx2         download_fx2.cpp         -L ../lib -l cyusb
	g++ -o download_fx3         download_fx3.cpp         -L ../lib -l cyusb
	g++ -o cyusbd               cyusbd.cpp               -L ../lib -l cyusb
	gcc -o config_parser        config_parser.c          -L ../lib -l cyusb

clean:
	rm -f 00_fwload 01_getdesc 03_getconfig 04_kerneldriver 05_claiminterface 06_setalternate
	rm -f 08_cybulk 09_cyusb_performance download_fx2 download_fx3 cyusbd config_parser 

help:
	@echo	'make		would compile all source programs in this directory
	@echo	'make clean	would remove all object code in this directory
