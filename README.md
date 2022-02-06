# Rimworld to Universal-VTT converter

This program reads [Rimworld](https://rimworldgame.com) savegame files and [ProgressRenderer](https://github.com/Lanilor/Progress-Renderer) images and converts them into Universal-VTT map files, which in turn can then be imported in many virtual tabletop game systems.

rim2vtt imports constructed walls and terrain data as well as light sources.

This has so far only been tested with [FoundryVTT](https://foundryvtt.com/). Feel free to test others and report any issues you find.

## online version

If you do not have access to a x64 linux machine you can use the [online version](https://vtt.brennecke-it.net/rim2vtt).
The source code can be found [here](https://github.com/SIGSEGV111/rim2vtt-hosted).

## ProgressRenderer

ProgressRenderer is a mod for Rimworld which takes images of your colony in configurable intervals.
This is foremost useful to generate a timelapse of your colonies development.
For it to work well with rim2vtt you should configure it as follows:

In the mod settings dialog:
- Pixels per Cell: 64
- Format: whatever your VTT system supports

On the map:
- Place render markers to limit the image area (optional)

## usage

`./rim2vtt /path/to/savegame_file /path/to/image_file > /path/to/output_uvtt_file`

## building from source

complicated...
You would need the el1-lib first.
The el1-lib is my personal convinience c++ lib and it only supports x64 linux at the moment (I do not have anything else).
I'm planning on porting it to Windows and ARM (32+64 bit) - but this will take some time (and Windows is not exactly high on my priority list eighter).

For the foolhardy:
1. clone el1-lib (it is linked as a submodule so just do a recursive clone)
2. build el1-lib by executing `dev-compile.sh` (and pray it works on your machine...)
3. run `make` on rim2vtt
4. profit :-)

## LICENSE

All files without license text are covered by `LICENSE.txt`.

`base64.c` and `base64.h`:

Copyright (c) 2003 Apple Computer, Inc. All rights reserved.

Copyright (c) 1995-1999 The Apache Group.  All rights reserved.

## Pictures

[<img src="https://raw.githubusercontent.com/SIGSEGV111/rim2vtt/master/walls.png">](https://raw.githubusercontent.com/SIGSEGV111/rim2vtt/master/walls.png)
