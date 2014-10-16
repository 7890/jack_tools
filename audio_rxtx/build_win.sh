#!/bin/bash
# this script creates a windows32 version of audio_rxtx
# cross-compiled on GNU/Linux
#
# this script is largely inspired by x-mingw.sh for Ardour / R. Gareus
#
# It is intended to run in a pristine chroot or VM of a minimal
# ubuntu system. see http://wiki.debian.org/cowbuilder
# but it can also be run as root on any system...
#
###############################################################################
### Quick start
### one-time cowbuilder/pbuilder setup on the build-host
#
# the following commands are good to build on a 64 bit host (ubuntu)
# for 32 bit hosts: 'amd64' -> 'i386'
#
# sudo apt-get install cowbuilder util-linux
# sudo mkdir -p /var/cache/pbuilder/trusty-amd64/aptcache
#
# sudo cowbuilder --create \
#     --basepath /var/cache/pbuilder/trusty-amd64/base.cow \
#     --distribution trusty \
#     --mirror "http://ch.archive.ubuntu.com/ubuntu/" \
#     --components "main universe"
#     --debootstrapopts --arch --debootstrapopts amd64
#
# this can take a few minutes
#
# now copy Makfefile.win and this script to /var/tmp
#
### 'login'
#
# sudo cowbuilder --login --bindmounts /var/tmp \
#     --basepath /var/cache/pbuilder/trusty-amd64/base.cow
#
# then call this (the one you read) script
#
# time /var/tmp/build_win.sh
# -> if the build proceeds well there will be /tmp/audio_rxtx_(number).tar
#

###############################################################################

# the standard variables are normally safe to use
# to skip the build stack this script can be started (from inside cow):
# NOSTACK=1 /var/tmp/build_win.sh
# this will update and build the audio_rxtx sources

: ${NOSTACK=}   # set to skip building the build-stack

# windows 32 bit binaries will/should work also on 64 bit 
: ${XARCH=i686} # or x86_64
: ${MAKEFLAGS=-j1}
: ${STACKCFLAGS="-O2 -g"}

: ${SRCDIR=/var/tmp/winsrc}  # source-code tgz cache
: ${TMPDIR=/var/tmp}         # package is built (and zipped) here.

: ${ROOT=/home/winbuild} # everything happens below here :)
                         # src, build and stack-install

###############################################################################

if [ "$(id -u)" != "0" -a -z "$SUDO" ]; then
	echo "This script must be run as root in pbuilder" 1>&2
	exit 1
fi

echo "??? create build stack for win32 dependencies of audio_rxtx and create executables?"
echo "(this command must be started from inside (--login) the minimal system or VM)"
echo "please see comments in the script header how to bootstrap a minimal build system."
echo "   do you want to continue? (ctrl+c to abort)"
read a

###############################################################################
set -e

if test "$XARCH" = "x86_64" -o "$XARCH" = "amd64"; then
	echo "Target: 64bit Windows (x86_64)"
	XPREFIX=x86_64-w64-mingw32
	HPREFIX=x86_64
	WARCH=w64
	DEBIANPKGS="mingw-w64"
else
	echo "Target: 32 Windows (i686)"
	XPREFIX=i686-w64-mingw32
	HPREFIX=i386
	WARCH=w32
	DEBIANPKGS="gcc-mingw-w64-i686 g++-mingw-w64-i686 mingw-w64-tools mingw32"
fi

: ${PREFIX=${ROOT}/win-stack-$WARCH}
: ${BUILDD=${ROOT}/win-build-$WARCH}

apt-get -y install build-essential \
	${DEBIANPKGS} \
	git autoconf automake libtool pkg-config \
	curl unzip ed yasm cmake ca-certificates \
	ne dos2unix locate zip

#fixup mingw64 ccache for now
if test -d /usr/lib/ccache -a -f /usr/bin/ccache; then
	export PATH="/usr/lib/ccache:${PATH}"
	cd /usr/lib/ccache
	test -L ${XPREFIX}-gcc || ln -s ../../bin/ccache ${XPREFIX}-gcc
	test -L ${XPREFIX}-g++ || ln -s ../../bin/ccache ${XPREFIX}-g++
fi

###############################################################################

mkdir -p ${SRCDIR}
mkdir -p ${PREFIX}
mkdir -p ${BUILDD}

unset PKG_CONFIG_PATH
export XPREFIX
export PKG_CONFIG_PATH=${PREFIX}/lib/pkgconfig
export PREFIX
export SRCDIR

#prevent possible wrong $HOME env var, that could make ccache fail (cannot create /home/foo/.ccache)
export HOME=${PREFIX}
#export CCACHE_TEMPDIR=${PREFIX}/.ccache

if test -n "$(which ${XPREFIX}-pkg-config)"; then
	export PKG_CONFIG=/usr/bin/pkg-config
#       export PKG_CONFIG=`which ${XPREFIX}-pkg-config`
fi

function download {
echo "--- Downloading.. $2"
test -f ${SRCDIR}/$1 || curl -k -L -o ${SRCDIR}/$1 $2
}

function src {
download ${1}.${2} $3
cd ${BUILDD}
rm -rf $1
tar xf ${SRCDIR}/${1}.${2}
cd $1
}

function autoconfconf {
set -e
echo "======= $(pwd) ======="
#CPPFLAGS="-I${PREFIX}/include -DDEBUG$CPPFLAGS" \
	CPPFLAGS="-I${PREFIX}/include$CPPFLAGS" \
	CFLAGS="-I${PREFIX}/include ${STACKCFLAGS} -mstackrealign$CFLAGS" \
	CXXFLAGS="-I${PREFIX}/include ${STACKCFLAGS} -mstackrealign$CXXFLAGS" \
	LDFLAGS="-L${PREFIX}/lib$LDFLAGS" \
	./configure --host=${XPREFIX} --build=${HPREFIX}-linux \
	--prefix=$PREFIX $@
}

function autoconfbuild {
set -e
autoconfconf $@
make $MAKEFLAGS && make install
}

#function wafbuild was here

################################################################################
if test -z "$NOSTACK"; then
################################################################################

### jack headers, .def, .lib, .dll and pkg-config file from jackd 1.9.10
### this is a re-zip of file extracted from official jack releases:
### https://dl.dropboxusercontent.com/u/28869550/Jack_v1.9.10_32_setup.exe
### https://dl.dropboxusercontent.com/u/28869550/Jack_v1.9.10_64_setup.exe

download jack_win3264.tar.xz http://robin.linuxaudio.org/jack_win3264.tar.xz
cd "$PREFIX"
tar xf ${SRCDIR}/jack_win3264.tar.xz
"$PREFIX"/update_pc_prefix.sh ${WARCH}

download pthreads-w32-2-9-1-release.tar.gz ftp://sourceware.org/pub/pthreads-win32/pthreads-w32-2-9-1-release.tar.gz
cd ${BUILDD}
rm -rf pthreads-w32-2-9-1-release
tar xzf ${SRCDIR}/pthreads-w32-2-9-1-release.tar.gz
cd pthreads-w32-2-9-1-release
make clean GC CROSS=${XPREFIX}-
mkdir -p ${PREFIX}/bin
mkdir -p ${PREFIX}/lib
mkdir -p ${PREFIX}/include
cp -vf pthreadGC2.dll ${PREFIX}/bin/
cp -vf libpthreadGC2.a ${PREFIX}/lib/libpthread.a
cp -vf pthread.h sched.h ${PREFIX}/include

################################################################################
#git://liblo.git.sourceforge.net/gitroot/liblo/liblo
#src liblo-0.28 tar.gz http://downloads.sourceforge.net/liblo/liblo-0.28.tar.gz
#autoconfconf --enable-shared
#ed src/Makefile << EOF
#/noinst_PROGRAMS
#.,+3d
#wq
#EOF
#ed Makefile << EOF
#%s/examples//
#wq
#EOF

cd ${BUILDD}
git clone -b fixmax https://github.com/7890/liblo.git liblo_28.fixmax
cd liblo_28.fixmax
./autogen.sh --disable-tests --disable-examples
autoconfconf --enable-shared --disable-tests --disable-examples
make $MAKEFLAGS && make install


################################################################################
fi  # $NOSTACK
################################################################################

cd ${BUILDD}
if [ -e jack_tools ]
then
	cd jack_tools/audio_rxtx
else
	git clone https://github.com/7890/jack_tools.git jack_tools
	cd jack_tools/audio_rxtx
fi

###
make -f Makefile.win all

EXT="`date +%s`"

APPSTRING=audio_rxtx_$EXT
DESTDIR=/tmp/$APPSTRING/

mkdir -p $DESTDIR/bin
cp -r build/audio_rxtx/* $DESTDIR

cp /usr/i686-w64-mingw32/lib/libwinpthread-1.dll $DESTDIR/bin
cp /usr/lib/gcc/i686-w64-mingw32/4.8/libgcc_s_sjlj-1.dll $DESTDIR/bin
cp ${PREFIX}/bin/liblo-7.dll $DESTDIR/bin
cp ${PREFIX}/bin/pthreadGC2.dll $DESTDIR/bin
cp ${PREFIX}/bin/oscdump.exe $DESTDIR/bin
cp ${PREFIX}/bin/oscsend.exe $DESTDIR/bin

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
