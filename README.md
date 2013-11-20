OSD Monitors
============

Introduction
------------

A simple, ultra-lightweight, transparent, unobtrusive X textual clock, cpu meter, network meter etc.. displayed as an overlay on the screen.

Features
--------

 * Uses libxosd2 for achieving pseudo-transparency. Floats  on  top  of all  windows  (is  shaped and unmanaged), everything but the letters is transparent.
 * User definable string format, font, position, outline, drop  shadow.
 * Intelligent unit display (automatically switches to kB/MB/.... units depending on value).
 * User definable colors for different values (CPU utilization can show green/yellow/red depending on value).
 * Can hide/show/toggle visibility upon signal receive. Define keyboard shortcuts in your favourite WM and toggle visibility when the  monitors obscure some part of the screen you need to see.
 *  Compact, easy to modify GPLed C source code you can alter to suit your needs.

.As of version 0.1, following monitors are implemented:
 * clock
 * cpu activity
 * context switches per second
 * processes in the Running state
 * used memory
 * used swap
 * swapping activity
 * used disk space
 * disk activity
 * network activity

If you happen to write some more monitors or find some bugs, link:contact.html[send me patches or reports].

Screenshots
-----------

OSD Monitors in action in the bottom right part of the screen
![Screenshot](http://ava.wz.cz/images/osd_monitors.png)


Installation
------------

Depends on libxosd, under debian:

```
# apt-get install libxosd-dev
```

or, maybe


```
# apt-get install libxosd2-dev
```

To compile, type 'make'.

To install, type 'make install' or simply copy the resulting binary anywhere
you like.

Running
-------

To run, see 

```
$ osd_monitors --help 
```

or the included example script 

```
$ run_osd_monitors
```

There is also a manual page included, which describes the usage in greater detail


Authors
-------
Tomáš Dvořák

Initially based on osd_clock by jon beckham <leftorium@leftorium.net>
