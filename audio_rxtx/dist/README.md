```
.deb created with checkinstall

.rpm derived from .deb with alien

packages will install to PREFIX /usr

(experimental, use at your own risk)

tested on
-x86_64 (ubuntu 12.04, jackd1)
-i686 (debian 7, ubuntu 14.04, jackd1)
-arm (ubuntu 13.04, jackd2)

install:
-------

on debian style:
sudo dpkg -i audio-rxtx.deb //(use exact name of .deb file)

uninstall:
---------

on debian style:
sudo apt-get remove audio-rxtx

issues:
-------

if during .deb / .rpm install packages are indicated as missing, this 
doesn't mean necessarily it won't work. the package might have another 
name on your distribution or you might have installed the needed 
dependencies by other means. needed package names are equal or similar to:

-liblo-dev (osc library, liblo.pc)
-libjack-dev (jack audio connection kit library, jack.pc)

example error output
---
Unpacking audio-rxtx (from audio-rxtx_0-1_armhf.deb) ...
dpkg: dependency problems prevent configuration of audio-rxtx:
 audio-rxtx depends on libjack-dev; however:
  Package libjack-dev is not installed.

dpkg: error processing audio-rxtx (--install):
 dependency problems - leaving unconfigured
Processing triggers for man-db ...
Errors were encountered while processing:
 audio-rxtx
---

consider to use --force-depends:
sudo dpkg -i --force-depends audio-rxtx_0-1_armhf.deb  

```
