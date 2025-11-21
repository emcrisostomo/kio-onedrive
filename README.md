Hello there, lonely wanderer and welcome to the magical land of Microsoft OneDrive!

# Building with kde-builder

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

# Setting up the CLI to run commands from the build tree

The KIO worker is built by default into the `~/kde` prefix, and the easiest way
to try it out before installing it into your host system is by running it from
there.  `kde-builder` provides an easy way to do it:

```sh
source ~/kde/build/kio-onedrive/prefix.sh
```

Sourcing the auto-generated `prefix.sh` script configures your shell to run
programs from the `~/kde` build tree (if found).

# Configuring an account

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

![Online Accounts](https://raw.githubusercontent.com/emcrisostomo/kio-onedrive/master/pictures/kaccounts-new.png)

# Using the worker

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

# Known issues

Some OneDrive metadata is not exposed
  Microsoft Graph omits sizes for virtual items (Office Online placeholders, shared
  documents that haven’t been downloaded, etc.). Those entries may show “Unknown”
  size inside Dolphin even though they open fine in Office Online.

Folders have "Unknown" size
  We cannot provide size information on folders, so KIO/Dolphin shows “Unknown”
  rather than recursing through every subfolder (which would waste bandwidth and
  be extremely slow on large cloud accounts).
