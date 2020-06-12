sfm
====
[![Language grade: C/C++]()]()
[![Build status]()]()
[![License]()]()

sfm is a simple {description} for unix-like systems, follows
the suckless [philosophy](https://suckless.org/philosophy/) and
[code style](https://suckless.org/coding_style/).

**current**
```sh
git clone https://github.com/afify/sfm.git
cd sfm/
make
make install
```
**latest release**
```sh
wget --content-disposition $(curl -s https://api.github.com/repos/afify/sfm/releases/latest | tr -d '",' | awk '/tag_name/ {print "https://github.com/afify/sfm/archive/"$2".tar.gz"}')
tar -xzf sfm-*.tar.gz && cd sfm-*/
make
make install
```
Run
---
```sh
$ sfm
```
Options
-------
```sh
$ sfm [-v]
$ man sfm
```
| option | description                                  |
|:------:|:---------------------------------------------|
| `-v`   | print version.                               |

Configuration
-------------
The configuration of sfm is done by creating a custom config.h
and (re)compiling the source code.
