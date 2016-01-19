#!/bin/bash

##call after build_win.sh successfully has built stack

PREFIX="/home/winbuild/win-stack-w32/"

export PKG_CONFIG_PATH="$PREFIX"/lib/pkgconfig/

make -f Makefile.win || exit 1

EXT="`date +%s`"

APPSTRING=jack_playfile_$EXT
DESTDIR=/tmp/$APPSTRING/

mkdir -p $DESTDIR/bin
mkdir -p $DESTDIR/doc

cp jack_playfile.exe $DESTDIR/bin
cp doc/jack_playfile.pdf $DESTDIR/doc

cp /usr/i686-w64-mingw32/lib/libwinpthread-1.dll $DESTDIR/bin
cp /usr/lib/gcc/i686-w64-mingw32/4.8/libgcc_s_sjlj-1.dll $DESTDIR/bin
##cp /usr/lib/gcc/i686-w64-mingw32/4.8/libstdc++-6.dll $DESTDIR/bin

#ls -1 "$PREFIX"/bin

cp "$PREFIX"/bin/pthreadGC2.dll $DESTDIR/bin
#cp "$PREFIX"/bin/libmpg123-0.dll $DESTDIR/bin
#cp ${PREFIX}/bin/liblo-7.dll $DESTDIR/bin
#cp ${PREFIX}/bin/oscdump.exe $DESTDIR/bin
#cp ${PREFIX}/bin/oscsend.exe $DESTDIR/bin

chmod 700 $DESTDIR/bin/*

ls -l $DESTDIR/bin

cd $DESTDIR
cd ..
#tar cvf $APPSTRING.tar $APPSTRING/
zip -r $APPSTRING.zip $APPSTRING/

ls -l $APPSTRING.zip
echo "(in `pwd`)"

updatedb

echo "done!"
