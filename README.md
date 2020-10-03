*squeekboard* - a Wayland virtual keyboard
========================================

*Squeekboard* is a virtual keyboard supporting Wayland, built primarily for the *Librem 5* phone.

It squeaks because some Rust got inside.

Features
--------

### Present

- GTK3
- Custom yaml-defined keyboards
- DBus interface to show and hide
- Use Wayland input method protocol to show and hide
- Use Wayland virtual keyboard protocol

### Temporarily dropped

- A settings interface

### TODO

- Use Wayland input method protocol
- Pick up DBus interface files from /usr/share

Building
--------

### Dependencies

See `.gitlab-ci.yml` or run `apt-get build-dep .`

### Build from git repo

```bash
$ git clone https://source.puri.sm/Librem5/squeekboard.git
$ cd squeekboard
$ mkdir _build
$ meson _build/
$ cd _build
$ ninja
```

To run tests use `ninja test`. To install squeekboard run `ninja install`.

Running
-------

```bash
$ phoc # if no compatible Wayland compositor is running yet
$ cd ../build/
$ src/squeekboard
```

Squeekboard honors the gnome "screen-keyboard-enabled" setting. Either enable this through gnome-settings under accessibility or run:

```bash
$ gsettings set org.gnome.desktop.a11y.applications screen-keyboard-enabled true
```

To make the keyboard show you can use either an application that does so automatically, like a text editor or `python3 ./tests/entry.py`, or you can manually trigger it with:

```bash
busctl call --user sm.puri.OSK0 /sm/puri/OSK0 sm.puri.OSK0 SetVisible b true
```

Developing
----------

See [`doc/hacking.md`](doc/hacking.md) for this copy, or the [official documentation](https://developer.puri.sm/projects/squeekboard/) for the current release.
