Hello there, lonely wanderer and welcome to the magical land of Microsoft OneDrive!

# Building with kde-builder

Using `kde-builder` is the recommended way to build and develop this project.
For more information, see [KDE developer documentation](https://develop.kde.org/docs/getting-started/building/kde-builder-setup/).

# Installation

    $ git clone https://github.com/emcrisostomo/kio-onedrive
    $ cd kio-onedrive
    $ mkdir build && cd build
    $ cmake -DCMAKE_INSTALL_PREFIX=`qtpaths --install-prefix` ..
    $ sudo make install
    $ kbuildsycoca6 # or log out/in to reload services

Now you are ready to use the worker. Either click the "Network" button in Dolphin or run:

    $ kioclient exec onedrive:/   # or kioclient6 on some distros

# Known issues

Some OneDrive metadata is not exposed
  Microsoft Graph omits sizes for virtual items (Office Online placeholders, shared
  documents that haven’t been downloaded, etc.). Those entries may show “Unknown”
  size inside Dolphin even though they open fine in Office Online.

Folders have "Unknown" size
  We cannot provide size information on folders, so KIO/Dolphin shows “Unknown”
  rather than recursing through every subfolder (which would waste bandwidth and
  be extremely slow on large cloud accounts).


# To do

Open tasks are tracked here: https://github.com/emcrisostomo/kio-onedrive/issues
