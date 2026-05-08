# virtualvaultfs

Ever suffered from this? You setup a gocryptfs/eCryptfs directory and start
copying your assets or your private client data, and then run out of path
length.

This happens because encrypted filenames are larger than plain text filenames.

Your path is consumed at 1.5x or so. What you see, is not what the underlying
filesystem gets.

A very long filename correctly stored in your non encrypted source, may not be
copyable to your enc directory due to wasted path length.

Well, virtualvaultfs is the solution.

We put an extra fs layer on top of gocryptfs (or any other) and save files
to generic names like 1343423.blob, while the full path is stored in a SQLite
db file. Then you can have full length even with encrypted filenames.

With our solution, that very large path consumed nothing from your internal
virtual fs. You still have full path usable.

Visual map:

- .gocrypt_source_dir: non usable filenames and file contents.

- gocrypt_mounted_dir: displayable ok in file browsers, writes to 
  .gocrypt_source_dir

  - .vvault: lives in gocrypt_mounted_dir, preferable at the root. Stores files
    in vault-data/objects/{id-digits}.blob, stores full filenames in metadata.
    sqlite3, also subdirs and symlinks, they don't exist physically and are only
    usable when mounting. symlinks are actually files, but they reference the
    expanded path, only usable when mounted.

  - vvault_mounted_dir: display full filenames correctly. Share this on SMB or
    NFS. It displays files with their original filename, no matter how long
    their path is, subdirectories and symlinks are usable and displayed
    correctly.

Note that .vvault directory should live inside gocrypt_mounted_dir, but
vvault_mounted_dir can be anywhere. You are still encrypting files stored there.
It's only a mount point. Put it where it benefits you more.

Is gocryptfs/eCryptfs mandatory? No. virtualvaultfs doesn't depend on it to
compile or function properly. If you are only fighting path length limitations,
and you don't care about encryption, you can put your .vvault directory outside
an encrypted mount point. It's up to you and your use case.

Also note that virtualvaultfs doesn't encrypt anything. It doesn't call
gocryptfs in any way. You are supposed to layer all this correctly. First
mount your enc directory, then mount virtualvaultfs on top of it. Only the
source initialized dir needs to live inside the encrypted dir.

Is virtualvaultfs useful without gocryptfs? Yes, but you only get very long
paths. It's not the use case of the original author (me), but it's a valid use.

Do I get infinite path length? Yes and no. By design, possible path string
length is limited only by sqlite3 capabilities. In practice, libraries and
applications like your filesystem browser of choice, may still do checks against
system constants defined in headers, etc. So, your file browser may still
behave inconsistently if you enter an abnormally large path. But it solves
the problem of eCryptfs having a maximum path length too short to be useful
for some use cases.

Windows? Sorry, no. In theory, with cygwin or WSL (Windows 10/11) may make
possible to build and run virtualvaultfs. Original author wrote this software
targeting file servers running Linux. Where the final layer is shared via SMB,
and the database file is not exposed. You still can have a Windows client
accessing and writing files to a shared directory mounted server side with
virtualvaultfs without any issue.


## Build

Make sure you have all dependencies:

```sh
sudo apt install build-essential cmake ninja-build pkg-config
sudo apt install libfuse3-dev libsqlite3-dev
```

cd into project root directory and run:

```sh
./scripts/build-debug.sh
./scripts/build-release.sh
```

Note for Ubuntu 22.04.

While Ubuntu 24.04 and newer compile with just the dependencies listed above,
this project requires "CMAKE_CXX_STANDARD 20", in particular we love %lt;format%gt;.

Ubuntu 22.04 default compiler doesn't support 20 standard fully. So it will miss
includes.

The fastest solution is to register this repo:

```sh
sudo add-apt-repository ppa:ubuntu-toolchain-r/test -y
```

And then install g++-13:

```sh
sudo apt update
sudo apt install g++-13 -y
```

Then, just build using the script build-release.sh. The script contains logic
to detect presence of g++-13 binary. If it is found, it will be used.


## Test

```sh
cmake --build build/debug
ctest --test-dir build/debug --output-on-failure
```


## Usage

Firs create an empty directory and initialize it:

```sh
mkdir /path/to/.vvault
virtualvaultfs initialize /path/to/.vvault
```

That will create the required directory layout and an empty database. This is
enough for future commands to recognize it as a correctly initialized directory.

Then mount:

```sh
virtualvaultfs mount /path/to/.vvault /path/to/vvault_mountpoint 
```

Do not kill the process, vvault_mountpoint will remain in invalid state and not
mountable again. Use CTRL+C to exit gracefully, it will unmount.

If a mountpoint remains in invalid state after process exit, and you get error
"Transport endpoint is not connected" or similar, manual unmount with:

```sh
fusermount3 -uz /path/to/vvault_mountpoint 
```

Then you can mount it again.

Be careful with your treatment of the /path/to/.vvault directory. Its database
contains the index of your files and directory structure. While physical files
are in .blob files named after their id in database.

Once you store something in virtualvaultfs, accessing your files will be very
difficult if you waste the database. The physical files will still be there,
but you have no way of knowing their original filenames or file extensions.

Backup the database from time to time is recommended, to be protected from
eventual corruption.

If you have automatized backups (mirror or snapshots) then you probably don't
need to do any manual backup of the database file. Your mirror, or snapshots,
have you covered.


## Share on SMB or NFS

You want the initialized directory, the one containing the database file, as
hidden as possible. Do not share that one. Share the mountpoint. And because
this is fuse (filesystem in user space) you need to enable "allow_other" for
the mountpoint, mounted as non root, to be accessible by SMB process.

This way you do not expose the database file. Any corruption there will not
waste your files contents, but will make very difficult to recover the original
filenames, as they are named with a pattern like 32423423.blob, and also would
make very difficult to recover original path, as paths aren't stored as real
directories (to avoid consumption of max path length) but only as records in the
database (this is what gives you, virtually, infinite path lengths).

Do this:

- First edit /etc/fuse.conf

- Uncomment "user_allow_other" and save changes.

- When executing virtualvaultfs mount use "-o allow_other", like this:

```sh
virtualvaultfs mount /path/to/.vvault_initialized_dir /path/to/vvault_mountpoint -o allow_other
```

In first release "-o allow_other" must be at the end of the command.

Sharing the root directory containing the .vvault initialized directory and then
mount at the remote side? Not currently possible. SQLite3 requires locking for
exclusive access, and we have that hardcoded in this release.

It may only work on NFS, but not on SMB.

No argument exist to change that behavior. Sorry. Right now, do not fight this.
Just accept it. Share your final mount point, not the initialized source vault
directory.


## Possible future evolutions

Because we already add a layer on top of the real filesystem, or on top of
another layer if you use gocryptfs, we already got hit by some minor performance
drop.

Because we are already there, why not having more features like distribute of
blobs among more than a single physical drive, or have snapshot capabilities.

Having those directly on virtualvaultfs would prevent having to stack more
fs layers. So, if you formatted your drive in ext4, and then realize you want
snapshots, you don't have to change your filesystem format, just mount
virtualvaultfs on top of it.

But all that is more of a fantasy right now, than a serious proposal. Design
such features is a complex task with many challenges.

Right now, virtualvaultfs does a single thing: solve your path length problems
by using existing solutions (sqlite3, fuse) in a smart way.


## Use with care

Original author is already using it in its own file server. But this is first
iteration. Non anticipated situations may exist.

More testers are welcome. Stress it. Be creative. Then report back. Help make
this solution a mature one, please.
