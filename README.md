<img src="https://afify.dev/img/sfm.png" alt="sfm logo"/>

**simple file manager**

[![Language grade: C/C++](https://img.shields.io/lgtm/grade/cpp/g/afify/sfm.svg?logo=lgtm&logoWidth=18)](https://lgtm.com/projects/g/afify/sfm/context:cpp)
[![Build status](https://ci.appveyor.com/api/projects/status/goq88ahjyvtjrui2?svg=true)](https://ci.appveyor.com/project/afify/sfm)

Description
------------
sfm is a simple file manager for unix-like systems.
* pthreads(7) to read events, no timers.
* BSD kqueue(2) - kernel event notification mechanism.
* Linux inotify(7) - monitoring filesystem events.
* dual pane.
* bookmarks.
* open files by extension.
* bottom statusbar.
* vim-like key bindings.
* no dependencies.
* c99.
* Inspired by [vifm](https://vifm.info/) and [noice](https://git.2f30.org/noice/).
* Follows the suckless [philosophy](https://suckless.org/philosophy/).

Patches
-------
[sfm-patches](https://github.com/afify/sfm-patches)

Performance
------------
```sh
$ perf stat -r 10 sfm
```

Options
-------
```sh
$ sfm [-v]
$ man sfm
```
<img src="https://afify.dev/img/sfm_sc.png" alt="sfm screenshot" width="800"/>

Installation
------------
<a href="https://repology.org/project/sfm-afify/versions">
    <img src="https://repology.org/badge/vertical-allrepos/sfm-afify.svg" alt="Packaging status">
</a>

**current**
```sh
git clone https://github.com/afify/sfm
cd sfm/
make
make install
```
**latest release**
```sh
latest=$(curl -s https://api.github.com/repos/afify/sfm/releases/latest | grep -o '"tag_name": "[^"]*' | cut -d'"' -f4)
tgz="https://github.com/afify/sfm/archive/refs/tags/${latest}.tar.gz"
curl -L -o "sfm-${latest}.tar.gz" "${tgz}"
tar -xzf "sfm-${latest}.tar.gz"
cd "sfm-${latest#v}" && \
make && make install || echo "Build failed!"
```

Run
---
```sh
$ sfm
```

Configuration
-------------
The configuration of sfm is done by creating a custom config.h
and (re)compiling the source code. This keeps it fast, secure and simple.
