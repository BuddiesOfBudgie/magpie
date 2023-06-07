# Magpie

Magpie is a X11 window manager and compositor library.

It contains functionality related to, among other things, window management,
window compositing, focus tracking, workspace management, keybindings and
monitor configuration.

Internally it uses a fork of Cogl, a hardware acceleration abstraction library
used to simplify usage of OpenGL pipelines, as well as a fork of Clutter, a
scene graph and user interface toolkit.

Magpie is a soft-fork of GNOME's mutter at version 43 tailored to the requirements of the Budgie Desktop 10 series (from v10.8 and later).  Magpie allows mutter based desktops such as GNOME Shell to co-exist since the key-components such as libmagpie are separated by both name and file-system install location.  Magpie shares some Mutter shared files; therefore these need to be delivered/installed as part of the distribution from its mutter package.  Alternatively these files can be delivered using the meson option "with_shared_components" where budgie-desktop is not required to co-exist with any mutter based desktops.

```
/lib/udev/rules.d/61-mutter.rules
/usr/share/GConf/gsettings/mutter-schemas.convert
/usr/share/glib-2.0/schemas/org.gnome.mutter.gschema.xml
/usr/share/glib-2.0/schemas/org.gnome.mutter.wayland.gschema.xml
/usr/share/glib-2.0/schemas/org.gnome.mutter.x11.gschema.xml
/usr/share/gnome-control-center/keybindings/50-mutter-navigation.xml
/usr/share/gnome-control-center/keybindings/50-mutter-system.xml
/usr/share/gnome-control-center/keybindings/50-mutter-wayland.xml
/usr/share/gnome-control-center/keybindings/50-mutter-windows.xml
```

Magpie is used by the Budgie Desktop as its window manager. It can also be run standalone, using
the  command "magpie", but just running plain magpie is only intended for
debugging purposes.

## Contributing

To contribute, open merge requests at https://github.com/buddiesofbudgie/magpie

It can be useful to look at the documentation available at the
[Wiki](https://gitlab.gnome.org/GNOME/mutter/-/wikis/home).

The API documentation is available at:
- Meta: <https://gnome.pages.gitlab.gnome.org/mutter/meta/>
- Clutter: <https://gnome.pages.gitlab.gnome.org/mutter/clutter/>
- Cally: <https://gnome.pages.gitlab.gnome.org/mutter/cally/>
- Cogl: <https://gnome.pages.gitlab.gnome.org/mutter/cogl/>
- CoglPango: <https://gnome.pages.gitlab.gnome.org/mutter/cogl-pango/>

## Coding style and conventions

See [HACKING.md](./HACKING.md).

## Git messages

Commit messages should follow the [GNOME commit message
guidelines](https://wiki.gnome.org/Git/CommitMessages). We require an URL
to either an issue or a merge request in each commit. Try to always prefix
commit subjects with a relevant topic, such as `compositor:` or
`clutter/actor:`, and it's always better to write too much in the commit
message body than too little.

## Default branch

The default development branch is `main`. 
```

## License

Magpie is distributed under the terms of the GNU General Public License,
version 2 or later. See the [COPYING][license] file for detalis.

[bug-tracker]: https://github.com/buddiesofbudgie/magpie/issues
[license]: COPYING
