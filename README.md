## About

**larper** is a small C program that listens to keyboard and mouse events in the background and plays sounds. it's extremely lightweight and performant, often taking less than 10mb of RAM.

## Motivation

I was using [MechVibes](https://github.com/hainguyents13/mechvibes) but it bugged me that it took over 300mb of RAM for what I was certain could be done way lighter, so I made this.

This doesn't even have any crazy optimizations or anything, it just listens to keys from /dev/input and plays wav files. No clue why anyone needs a GUI for that.

## Installation

Download the [latest release](https://github.com/loremdifr/larper/releases) or compile from source with `make release`.

`larper` is a single executable, and it comes with a helper `importer` (also an executable). [See below](#importing-sound-packs-from-mechvibes) for more info on the importer.

**IMPORTANT:** This program is linux only and requires the `input` usergroup to be run properly as it directly listens to `/dev/input`, so ensure your user has access to it:

```bash
groups | grep "input"
```

if you don't see "input" highlighted after running that command, add yourself to the "input" group:

```bash
sudo usermod -aG input $USER
```

## Usage

```bash
larper run /path/to/your/folder/
```

**IMPORTANT:** You need a folder where there is a WAV file for each key you want to emit a sound.

use `larper help` for more information.


## Configuration

larper needs no configuration, it works by convention.
Point it to a folder where the sound files match the expected names, and it will "just" work.

for a list of expected filenames, use:

```bash
larper keys
```

## Sound Packs

There's no concept of a *sound pack* in larper.
this program simply uses the WAV files located in the path you point it to.

The reasoning for this is simple:

1. it's much easier for users to tweak each sound file and combine packs to their hearts content if it's simply a folder of sound files. This also means there's no lock-in whatsoever, as sounds are just sound files, not "Larper™ Sound Packs".
2. to keep larper as lightweight as it is, it should do only as much work as it needs to and nothing more.


## Importing Sound Packs from MechVibes

larper comes with a simple importer utility that you can use to convert [Sounds Packs from MechVibes](https://mechvibes.com/sound-packs) into the format that larper expects (a folder with wav files).

to use the importer, simply run:

```bash
importer <zipfile>
```

where `<zipfile>` is the path to zip file you download from the [MechVibes website](https://mechvibes.com/sound-packs).

this will produce the new folder in the location you call the importer from, prefixed with "converted_".

## Using Mouse Sound Packs

unlike MechVibes, larper makes no distinction between keyboard and mouse sound packs, you can use either. You can also mix them by simply combining them into one folder.
