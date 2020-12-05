![sfm](https://github.com/afify/sfm/blob/main/sfm.png?raw=true)

**simple file manager**

[![Language grade: C/C++](https://img.shields.io/lgtm/grade/cpp/g/afify/sfm.svg?logo=lgtm&logoWidth=18)](https://lgtm.com/projects/g/afify/sfm/context:cpp)
[![Build status](https://ci.appveyor.com/api/projects/status/goq88ahjyvtjrui2?svg=true)](https://ci.appveyor.com/project/afify/sfm)

Description
------------
sfm is a simple file manager for unix-like systems based on [termbox](https://github.com/nsf/termbox).
dual pane, bottom statusbar, bookmarks, open files by extention, vim-like key bindings as default configuration. cwd is left pane dir. No dependencies, static linking, c99.

* Inspired by [vifm](https://vifm.info/) and [noice](https://git.2f30.org/noice/).
* Follows the suckless [philosophy](https://suckless.org/philosophy/) and [code style](https://suckless.org/coding_style/).

<img src="https://github.com/afify/afify.github.io/raw/main/img/sfm_sc.png" alt="drawing" width="800"/>

Options
-------
```sh
$ sfm [-v]
$ man sfm
```

**normal mode**
| key      | description         |
|:---------|:--------------------|
| `q`      | quit                |
| `h`      | back                |
| `j`      | down                |
| `k`      | up                  |
| `l`      | open                |
| `g`      | top                 |
| `G`      | bottom              |
| `M`      | middle              |
| `ctrl+u` | scroll up           |
| `ctrl+d` | scroll down         |
| `n`      | create new file     |
| `N`      | create new dir      |
| `d`      | delete file \| dir  |
| `y`      | yank                |
| `p`      | paste               |
| `P`      | move                |
| `c`      | rename              |
| `/`      | start filter        |
| `ENTER`  | find  filter        |
| `ESC`    | exit  filter        |
| `SPACE`  | switch pane         |

**visual mode**
| key      | description         |
|:---------|:--------------------|
| `v`      | enter visual mode   |
| `j`      | select down         |
| `k`      | select up           |
| `ESC`    | exit visual mode    |

Installation
------------
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

Configuration
-------------
The configuration of sfm is done by creating a custom config.h
and (re)compiling the source code.
