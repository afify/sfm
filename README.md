<img src="https://afify.dev/img/sfm.png" alt="sfm logo"/>

**simple file manager**

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
* filter.
* no dependencies.
* c99 static linking.
* based on [termbox](https://github.com/nsf/termbox).
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
git clone git://git.afify.dev/sfm
cd sfm/
make
make install
```
**latest release**
```sh
[ "$(uname)" = "Linux" ] && shacmd="sha256sum" grepf="--color=never"|| shacmd="sha256"
latest=$(curl -s https://git.afify.dev/sfm/tags.xml | grep $grepf -m 1 -o "\[v.*\]" | tr -d '[]')
tgz="https://git.afify.dev/sfm/releases/sfm-${latest}.tar.gz"
sha="${tgz}.sha256"
wget "${tgz}"
wget "${sha}"
${shacmd} -c "sfm-${latest}.tar.gz.sha256" && \
tar -xzf "sfm-${latest}.tar.gz" && cd "sfm-${latest}" && \
make
make install
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
