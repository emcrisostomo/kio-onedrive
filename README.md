# What is this?

This project provides a KDE KIO worker that surfaces Microsoft OneDrive storage
directly inside KDE apps and file managers.  It hooks into KDE Online Accounts
so you can add multiple Microsoft accounts, browse drives (including shared
items), and upload/download files **as if they were local**.

If you need to setup a Microsoft provider for your own tenant, or with a
specific client ID, please read [README.enterpriseapps.md](README.enterpriseapps.md).

## Features

* Multiple OneDrive accounts via KDE Online Accounts, including an in-worker
“New account” entry.
* Browse drives and folders, including a “Shared With Me” virtual folder.
  * Note: Microsoft has deprecated this API (see [API refence](https://learn.microsoft.com/en-us/graph/api/drive-sharedwithme?view=graph-rest-1.0&tabs=http))
  and no official alternatives have been announced yet.
* Download/open files with correct MIME detection.
* Create folders and upload/overwrite files in personal OneDrive content.
* Rename, copy/move, and delete entries inside personal drives (shared drives
are not yet supported).
* Reports quota and free space metadata for account paths.
* Integration with context menu actions.
* Integration with an additional OneDrive pane in the properties dialog.

## Limitations

The worker issues Microsoft Graph API requests synchronously: as a consequence,
long uploads or downloads can block the worker until the reply returns.
Cancellation and responsiveness are limited during those operations.

## Building with kde-builder

Using `kde-builder` is the recommended way to build and develop this project.
For more information, see [KDE developer documentation](https://develop.kde.org/docs/getting-started/building/kde-builder-setup/)
and [kde-builder documentation](https://kde-builder.kde.org/en/).

## Checking out the sources

The path of minimal friction is cloning this repository into `~/kde/src`, which
is the default directory where `kde-builder` checks out project sources to
build.

```sh
cd ~/kde/src
git clone https://github.com/emcrisostomo/kio-onedrive.git
```

## Adding your local project

To add your local project to the list of projects known to `kde-builder` you can
add an entry into `~/.config/kde-builder.yaml`:

```yaml
project kio-onedrive:
  repository: "https://github.com/emcrisostomo/kio-onedrive.git"
  no-src: true
```

What this project configuration fragment says is essentially:

* `repository: ""`: this project repository URL.
* `no-src: true`: not to update the source code, skipping the project `update`
  phase.

## Configuring the Microsoft KAccounts provider

This project requires three configuration variables to be set in order to be
functional:

* `ONEDRIVE_CLIENT_ID`: Application (client) ID of your Azure App Registration.
* `ONEDRIVE_CLIENT_SECRET`: Client secret issued for that App Registration.
This will likely be an empty string for public clients.
* `ONEDRIVE_TENANT`: Tenant ID (GUID) or `common`/`organizations` depending on
your account scope.  For personal OneDrive accounts, use the `common` tenant.

These variables are require to configure the Microsoft KAccounts provider
this project will install into your system. Details on how to configure an
Azure App Registration are not part of this documentation.

If this project makes into KDE, a KDE-owned Microsoft provider will be
configured for you.

The included `Makefile` will read from a local `.onedrive.env` file and run
`kde-builder` for you.

## Building and installing the project

`kde-builder` manages all the project build lifecycle for you with just one
command:

```sh
kde-builder kio-onedrive
```

If you want to trigger a full rebuild instead of an incremental rebuild:

```sh
kde-builder kio-onedrive -r
```

## Setting up the CLI to run commands from the build tree

The KIO worker is built by default into the `~/kde` prefix, and the easiest way
to try it out before installing it into your host system is by running it from
there.  `kde-builder` provides an easy way to do it:

```sh
source ~/kde/build/kio-onedrive/prefix.sh
```

Sourcing the auto-generated `prefix.sh` script configures your shell to run
programs from the `~/kde` build tree (if found).

## Configuring an account

The first thing you need to do to get any meaningful feature out of this project
is configuring your own Microsoft account using the KAccounts provider.  From
a preconfigured shell (see previous section), run:

```sh
kcmshell6 kcm_kaccounts
```

This will open the Online Accounts panel of the KDE Settings:

![Online Accounts](https://raw.githubusercontent.com/emcrisostomo/kio-onedrive/master/pictures/kaccounts.png)

From there you will be able to list your existing Online Accounts and create a
new one choosing the _Microsoft_ provider:

![Create new Online Account](https://raw.githubusercontent.com/emcrisostomo/kio-onedrive/master/pictures/kaccounts-new.png)

## Using the worker

A KIO worker can be invoked from the CLI using `kioclient`.

This command will list your drives (you will notice a `new-account` meta-node
in the output which is used by KDE user interfaces):

```sh
kioclient ls "onedrive:/"
```

This command will list files into your drive, where `drive-name` is the name of
the account you set up earlier, which you can also list using the previous
command.

```sh
kioclient ls "onedrive:/drive-name"
```

Otherwise, you can just run `dolphin` and interact with OneDrive using the UI:

```sh
dolphin
```

or

```sh
kioclient exec "onedrive:/drive-name"
```

In Dolphin you will also see all your Online Accounts in the `Network` node:

![Dolphin: Network](https://raw.githubusercontent.com/emcrisostomo/kio-onedrive/master/pictures/network.png)

## Known issues

Some OneDrive metadata is not exposed
  Microsoft Graph omits sizes for virtual items (Office Online placeholders, shared
  documents that haven’t been downloaded, etc.). Those entries may show “Unknown”
  size inside Dolphin even though they open fine in Office Online.

Folders have "Unknown" size
  We cannot provide size information on folders, so KIO/Dolphin shows “Unknown”
  rather than recursing through every subfolder (which would waste bandwidth and
  be extremely slow on large cloud accounts).
