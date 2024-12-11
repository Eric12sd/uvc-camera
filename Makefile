camera:Camera-new.c
	arm-linux-gnueabihf-gcc -o Camera Camera-new.c -L /home/twk123/lib/arm/jpegsrc/lib -ljpeg -L /home/twk123/lib/arm/tslibsrc/lib -lts -lpthread \
	-L /home/twk123/lib/arm/libyuv/lib -lyuv -I /home/twk123/lib/arm/libyuv/include
clean:
	rm -f Camera
