# tty2socket

``tty2socket`` is a simple tool to forward a program's stdin and stdout
towards a UNIX socket.

## BUILD

This tool is single-filed and depends on the option ``SO_PEERCRED`` of
``getsockopt()``, which is GNU specified but works on ``musl`` as well.

```
$ cc tty2socket.c -o tty2socket
```

would even work fine.

## INSTALLATION

``tty2socket`` is available in eweOS.

```
$ pacman -S tty2socket
```

## OPTIONS

See attached help,which could be printed by ``tty2socket -h``

## LICENSE

By MIT License.
