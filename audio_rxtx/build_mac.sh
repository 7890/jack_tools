#!/bin/bash

liblo_uri="/usr/local/lib/liblo.7.dylib"

echo "this script can be executed on OSX system after a successful compilation of audio_rxtx with 'make'"
echo "liblo library to use: $liblo_uri"
echo "(see branch fixmax https://github.com/7890/liblo.git)"
echo "continue?"
read a

mkdir -p build/mac_binaries
cp build/jack_audio_send build/mac_binaries
cp build/jack_audio_receive build/mac_binaries
cp "$liblo_uri" build/mac_binaries

cur="`pwd`"

cd build/mac_binaries

echo "install_name_tool foo..."
install_name_tool -change /usr/local/lib/liblo.7.dylib @executable_path/liblo.7.dylib  jack_audio_send
install_name_tool -change /usr/local/lib/liblo.7.dylib @executable_path/liblo.7.dylib  jack_audio_receive
install_name_tool -id @executable_path/liblo.7.dylib liblo.7.dylib

echo "changed dependencies:"
otool -L jack_audio_send
otool -L jack_audio_receive

echo "moving $liblo_uri to $liblo_uri.tmp for testing dependency"
sudo mv "$liblo_uri" "$liblo_uri".tmp

echo "program output:"
./jack_audio_send --loinfo
./jack_audio_receive --loinfo

echo "moving $liblo_uri.tmp back to $liblo_uri"
sudo mv "$liblo_uri".tmp "$liblo_uri"

cd "$cur"

echo "binaries are in build/mac_binaries"
ls -l build/mac_binaries

