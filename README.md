Hello there, lonely wanderer and welcome to the magical land of Microsoft OneDrive!


INSTALLATION
============

    $ git clone https://invent.kde.org/network/kio-onedrive.git
    $ cd kio-onedrive
    $ mkdir build && cd build
    $ cmake -DCMAKE_INSTALL_PREFIX=`qtpaths --install-prefix` ..
    $ sudo make install
    $ kdeinit5 # or just re-login

Now you are ready to use the worker. Either click the "Network" button in Dolphin or run:

    $ kioclient6 exec onedrive:/


KNOWN ISSUES
============

Some OneDrive metadata is not exposed
  Microsoft Graph omits sizes for virtual items (Office Online placeholders, shared
  documents that haven’t been downloaded, etc.). Those entries may show “Unknown”
  size inside Dolphin even though they open fine in Office Online.

Folders have "Unknown" size
  We cannot provide size information on folders, so KIO/Dolphin shows “Unknown”
  rather than recursing through every subfolder (which would waste bandwidth and
  be extremely slow on large cloud accounts).


TODO
===========

Open tasks are tracked here: https://invent.kde.org/network/kio-onedrive/-/issues
