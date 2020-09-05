# iff2gif
#### Convert IFF ILBM/ANIM files to GIF files

ILBM and ANIM were very popular formats on the Amiga. ILBM also saw some limited use on other platforms in
the early '90s, while ANIM never really made its way off of the Amiga. This program will convert them both to GIF,
which has more widespread support across several platforms.

For animations, iff2gif tries to minimize the amount of data per frame. The first frame is stored in its entirety. Succesive
frames only store the rectangular regions that contain actual changes. Within that region, unchanged pixels may either be
stored as-is or substituted with a transparent color, depending on which iff2gif decides results in smaller image data.

### Usage

Usage: iff2gif [*options*] *input file* [*output file*]

The output file name is optional. If not provided, it will be generated by attaching a .gif extension to the input file name.

#### Options

* **-c *frame-list***  
  Clips out the specified frames from the input file and only writes those to the output file.
  This accepts both a single frame or a range of frames of the form *start*-*end*. If the start frame of a range
  is omitted, frame 1 is implied. If the end frame of a range is omitted, the range continues to the final frame
  of the input file. Multiple entries may be separated by commas.

  Examples:
  - **-c -50**  
    Write the first 50 frames.
  - **-c 50-**  
    Skip the first 49 frames, and write all the rest.
  - **-c 10,20,30**  
    Only write frames 10, 20, and 30.
  - **-c 1,15-20**  
    Write frame 1 and frames 15-20.

* **-f**  
  Write each frame to a separate file. If the output file name has a series of 0s
  at the end before the file extension, they will be replaced by the frame
  number. This will dynamically expand if the field width isn't large
  enough for the frame number. Note that 0s embedded in the middle of the filename will
  not be replaced with the frame number. They must be at the end of the filename.
    
  If there are no 0s to replace, then the frame number is inserted before the extension. In this case,
  if the input file contains a DPAN chunk, the frame count stored in it will be used to decide how many
  0s to pad with.
    
  Examples with the -f option:
  - For the output file name "funtime-00.gif", frame 1 will be written to "funtime-01.gif".
    Frame 12 will be written to "funtime-12.gif". If there is a frame 1000, it
    will be written to "funtime-1000.gif"
  - For the output file name "hello.gif" and the input file has a DPAN chunk that
    indicates 156 frames, frame 1 will be written to "hello001.gif".
    Frame 10 will be written to "hello010.gif".
    Frame 100 will be written to "hello100.gif".
  - For the output filename "world.gif" and the input file has no DPAN chunk, frame 1
    will be written to "world1.gif". Frame 10 will be written to "world10.gif".
    Frame 100 will be written to "world100.gif". And so on.</dd>

* **-n**  
  No aspect ratio correction. Normally hires and interlaced super hires images will
  be vertically doubled, super hires images will be vertically quadrupled, and interlaced
  lores images will be horizontally doubled in order to preserve the aspect ratio they
  had on the Amiga. This option disables that stretching.
  
  Aspect ratio correction is applied on top of the scaling specified with the
  -s, -x, or -y options.

* **-r *frame-rate***  
  Write the GIF with the specified frame rate instead of the one from the ANIM.

* **-s *scale***  
  Set both horizontal and vertical scale to the same value. Must be an integer
  greater than 0.

* **-x *X scale***  
  Set horizontal scale. Must be an integer greater than 0.

* **-y *Y scale***  
  Set vertical scale. Must be an integer greater than 0.

### Limitations
Deep ILBM files are not supported, as using true color with GIF is not exactly supported in any sort of standard way. Files
using HAM modes will be converted, but without any of the HAMming effect that makes them interesting to use on the Amiga.

### Build instructions
#### Requirements
##### Common
- CMake 3.18 or higher ([version for Windows](cmake.org/download), `cmake` package on all popular Linux distributions)
##### Windows
- Microsoft Visual Studio 2017 or higher with C++ workload selected (download [here](https://visualstudio.microsoft.com/ru/))
##### Linux
- `make`
- `gcc`
- `libc6-dev` package on Debian-based distributions, `glibc` on RPM and Arch-based distros

#### How to build
##### Windows
1. Open the Developer Command Prompt for Visual Studio.
2. Run `cd project_path`, substituting *project_path* correspondingly.
3. Run `cmake .` to generate a Visual Studio solution.
4. Either open `iff2gif.sln` in Visual Studio or run `msbuild -m iff2gif.sln` to build the solution from the command line.  
   `msbuild` will build a debug version by default, use `msbuild -m -p:Configuration=Release iff2gif.sln` to build a release version.  
   Executable file will be in either `Debug` or `Release` directory depending on the configuration.
##### Linux
1. Open a terminal window.
2. Run `cd project_path`, substituting *project_path* correspondingly.
3. Run `cmake .` to generate a Makefile.
4. Run `make` to build the project.  
   Executable file will be in the project root, you can run it with `./iff2gif`.