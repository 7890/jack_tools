#!/bin/bash
# this script creates a windows32 version of jack_playfile
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
# -> if the build proceeds well the dependency stack is ready
# then call ./build_win32_bundle.sh

###############################################################################

# the standard variables are normally safe to use
# to skip the build stack this script can be started (from inside cow):
# NOSTACK=1 /var/tmp/build_win.sh
# this will update and build the jack_playfile

: ${NOSTACK=}   # set to skip building the build-stack

: ${COPY_HOME=} # to copy all in $ROOT from a previously saved location
                # i.e. NOSTACK=1 COPY_HOME=1 /var/tmp/build_win.sh

: ${COPY_HOME_LOCATION="/var/tmp/winbuild_from_cow"} 

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

echo "??? create build stack for win32 dependencies of jack_playfile and create executables?"
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
	ne dos2unix locate zip rsync

cat /root/.bashrc | sed "s/alias l='ls -CF'/alias l='ls -ltr'/g" > /tmp/bashrc
cp /tmp/bashrc /root/.bashrc

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

echo "=================="
echo "PKG_CONFIG_PATH ${PKG_CONFIG_PATH}"
echo "=================="

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
echo "===========================new compile task"
echo "$(pwd)"
#CPPFLAGS="-I${PREFIX}/include -DDEBUG$CPPFLAGS" \
cat << EOF_
	CPPFLAGS="-I${PREFIX}/include$CPPFLAGS" \
	CFLAGS="-I${PREFIX}/include ${STACKCFLAGS} -mstackrealign$CFLAGS" \
	CXXFLAGS="-I${PREFIX}/include ${STACKCFLAGS} -mstackrealign$CXXFLAGS" \
	LDFLAGS="-L${PREFIX}/lib$LDFLAGS" \
	./configure --host=${XPREFIX} --build=${HPREFIX}-linux \
	--prefix=$PREFIX $@
EOF_
echo "===========================start configure `date`"

	CPPFLAGS="-I${PREFIX}/include$CPPFLAGS" \
	CFLAGS="-I${PREFIX}/include ${STACKCFLAGS} -mstackrealign$CFLAGS" \
	CXXFLAGS="-I${PREFIX}/include ${STACKCFLAGS} -mstackrealign$CXXFLAGS" \
	LDFLAGS="-L${PREFIX}/lib$LDFLAGS" \
	./configure --host=${XPREFIX} --build=${HPREFIX}-linux \
	--prefix=$PREFIX $@
echo "===========================configure done `date`"
}

function autoconfbuild {
set -e
autoconfconf $@
echo "===========================start make `date`"
make $MAKEFLAGS && make install
echo "===========================make done `date`"
}

#function wafbuild was here

if [ x"$COPY_HOME" = "x1" ]
then
	cp -ar "$COPY_HOME_LOCATION"/* ${ROOT}
fi

################################################################################
if test -z "$NOSTACK"; then
################################################################################

### jack headers, .def, .lib, .dll and pkg-config file from jackd 1.9.10
### this is a re-zip of file extracted from official jack releases:
### https://dl.dropboxusercontent.com/u/28869550/Jack_v1.9.10_32_setup.exe
### https://dl.dropboxusercontent.com/u/28869550/Jack_v1.9.10_64_setup.exe

#download jack_win3264.tar.xz http://robin.linuxaudio.org/jack_win3264.tar.xz
download jack_win3264.tar.xz https://raw.githubusercontent.com/7890/jack_tools/master/audio_rxtx/archive/win_build_deps/jack_win3264.tar.xz

cd "$PREFIX"
tar xf ${SRCDIR}/jack_win3264.tar.xz
"$PREFIX"/update_pc_prefix.sh ${WARCH}

#download pthreads-w32-2-9-1-release.tar.gz ftp://sourceware.org/pub/pthreads-win32/pthreads-w32-2-9-1-release.tar.gz
download pthreads-w32-2-9-1-release.tar.gz https://raw.githubusercontent.com/7890/jack_tools/master/audio_rxtx/archive/win_build_deps/pthreads-w32-2-9-1-release.tar.gz

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


src libogg-1.3.2 tar.gz http://downloads.xiph.org/releases/ogg/libogg-1.3.2.tar.gz
autoconfbuild

src libvorbis-1.3.5 tar.gz http://downloads.xiph.org/releases/vorbis/libvorbis-1.3.5.tar.gz
autoconfbuild --disable-examples --with-ogg=${PREFIX}

src flac-1.3.1 tar.xz http://downloads.xiph.org/releases/flac/flac-1.3.1.tar.xz
ed Makefile.in << EOF
%s/examples / /
wq
EOF
autoconfbuild --enable-static --disable-xmms-plugin --disable-cpplibs --disable-doxygen-docs 

src libsndfile-1.0.26 tar.gz http://www.mega-nerd.com/libsndfile/files/libsndfile-1.0.26.tar.gz
ed Makefile.in << EOF
%s/ examples regtest tests programs//
wq
EOF
LDFLAGS=" -lFLAC -lwsock32 -lvorbis -logg -lwsock32" \
autoconfbuild --disable-sqlite --disable-alsa
ed $PREFIX/lib/pkgconfig/sndfile.pc << EOF
%s/ -lsndfile/ -lsndfile -lvorbis -lvorbisenc -lFLAC -logg -lwsock32/
wq
EOF

#inlined
#src zita-resampler-1.3.0 tar.bz2 http://kokkinizita.linuxaudio.org/linuxaudio/downloads/zita-resampler-1.3.0.tar.bz2
#cd libs

#echo "building zita-resampler..."

#i686-w64-mingw32-g++ -Wall -O2 -ffast-math -march=native -I. -D_REENTRANT -D_POSIX_PTHREAD_SEMANTICS  -c -o resampler.o resampler.cc
#i686-w64-mingw32-g++ -Wall -O2 -ffast-math -march=native -I. -D_REENTRANT -D_POSIX_PTHREAD_SEMANTICS  -c -o vresampler.o vresampler.cc
#i686-w64-mingw32-g++ -Wall -O2 -ffast-math -march=native -I. -D_REENTRANT -D_POSIX_PTHREAD_SEMANTICS  -c -o resampler-table.o resampler-table.cc
#i686-w64-mingw32-g++ -shared -o libzita-resampler.dll resampler.o vresampler.o resampler-table.o  -Wl,-static,--out-implib,libzita-resampler.a
#cp libzita-resampler.dll ${PREFIX}/bin
#cp libzita-resampler.a ${PREFIX}/lib
#cp -r zita-resampler ${PREFIX}/include

#echo "done."

src opus-1.1 tar.gz http://downloads.xiph.org/releases/opus/opus-1.1.tar.gz
autoconfbuild --disable-doc --disable-extra-programs

src opusfile-0.6 tar.gz https://ftp.mozilla.org/pub/mozilla.org/opus/opusfile-0.6.tar.gz
autoconfbuild --disable-doc --disable-http #this is to prevent need for ssl

src mpg123-1.22.3 tar.bz2 http://mpg123.org/download/mpg123-1.23.4.tar.bz2
autoconfbuild ./configure --enable-static --with-module-suffix=.dll --enable-network=no --disable-id3v2 --with-default-audio=dummy --disable-messages
#set -e temporary off
make install
#ls -1 /home/winbuild/win-stack-w32/lib/libmpg123*
#/home/winbuild/win-stack-w32/lib/libmpg123.a
#/home/winbuild/win-stack-w32/lib/libmpg123.dll.a
#/home/winbuild/win-stack-w32/lib/libmpg123.la


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

#cd ${BUILDD}
#if [ -e liblo_28.fixmax ]
#then
#	cd liblo_28.fixmax
#	git pull
#else
#	git clone -b fixmax https://github.com/7890/liblo.git liblo_28.fixmax
#	cd liblo_28.fixmax
#fi

#./autogen.sh --disable-tests --disable-examples
#autoconfconf --enable-shared --disable-tests --disable-examples
#make $MAKEFLAGS && make install


################################################################################
fi  # $NOSTACK
################################################################################

cd ${BUILDD}
if [ -e jack_tools ]
then
	cd jack_tools/jack_playfile
	git reset --hard HEAD
	git pull
else
	git clone https://github.com/7890/jack_tools.git jack_tools
	cd jack_tools/jack_playfile
fi

###
#make -f Makefile.win

updatedb

echo "stack is ready!"
#echo "now call 'build_win32_bundle.sh' manually."

./build_win32_bundle.sh
