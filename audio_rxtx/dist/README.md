```
.deb created with checkinstall

.rpm derived from .deb with alien

packages will install to PREFIX /usr

(experimental, use at your own risk)

issues:

if during .deb / .rpm install packages are indicated as missing, this 
doesn't mean necessarily it won't work. the package might have another 
name on your distribution or you might have installed the needed 
dependencies by other means.

Unpacking audio-rxtx (from audio-rxtx_0-1_armhf.deb) ...
dpkg: dependency problems prevent configuration of audio-rxtx:
 audio-rxtx depends on jackd; however:
  Package jackd is not installed.

dpkg: error processing audio-rxtx (--install):
 dependency problems - leaving unconfigured
Processing triggers for man-db ...
Errors were encountered while processing:
 audio-rxtx

consider to use --force-depends:
sudo dpkg -i --force-depends audio-rxtx_0-1_armhf.deb  

```