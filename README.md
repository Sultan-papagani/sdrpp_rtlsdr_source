# sdrpp_rtlsdr_source
New rtl-sdr source for sdr++ which implements better controls for r820t2/r828d tuners.

![Screenshot_7](https://github.com/Sultan-papagani/sdrpp_rtlsdr_source/assets/69393574/ea42324f-73fd-43c3-96c2-1556c305746d)
![fd](https://github.com/Sultan-papagani/sdrpp_rtlsdr_source/assets/69393574/1131129e-5730-4121-ae8a-1814d941d9d2)

![Screenshot_6](https://github.com/Sultan-papagani/sdrpp_rtlsdr_source/assets/69393574/81a2cc14-760f-4827-a540-2684e754a09d)


## Features
* Filter Controls
* Software & Hardware Tuner AGC
* Basic & Manual Controls 
* Mixer, Lna, DAGC(Rtl Gain), Total Gain Readings
* Agc Clock Setting for faster/slower AGC
* Tuner IF Frequency shifting

## Needed Hardware
* rtl-sdr with a r820/r820t2/r828d tuner 
* RTL-SDR Blog V4 and V3 supported

# Installing

## Windows

Download prebuilt .dll files from Release
* put "librtlsdr.dll" and "libwinpthread-1.dll" in the same folder as sdrpp.exe
* Put "new_rtlsdr_source.dll" into "modules" folder
* Launch sdrpp.exe and add the new module from "Module Manager"
* Go to "Source" and select "NEW-RTL-SDR"

## SDR++ Server installation
* put "librtlsdr.dll" and "libwinpthread-1.dll" in the same folder as sdrpp.exe
* Put "sdrpp server\new_rtlsdr_source.dll" into "modules" folder (not the folder itself, only the .dll)
* Launch sdrpp.exe and add the new module from "Module Manager"
* Go to "Source" and select "NEW-RTL-SDR"

## Other

There are currently no existing packages for other distributions

# Building on Windows

1) Compile old-dab fork:
   ```
   https://github.com/Sultan-papagani/rtlsdr-olddab
   ```
   rename librtlsdr.dll.a to librtlsdr.lib <br />
   and librtlsdr_static.a to librtlsdr_static.lib <br />
   (i used mingw on windows) <br />

3) Add the "new_rtlsdr_source" folder to "SDRPlusPlus/source_modules"

4) On new_rtlsdr_source\CMakeLists.txt change the cmake paths to wherever you put the old-dab library

5) on SDRPlusPlus\CMakeLists.txt add
   ```
   option(OPT_BUILD_NEW_RTL_SDR_SOURCE "Build RTL-SDR Source Module (Dependencies: librtlsdr)" ON)
   ```
   ```
   if (OPT_BUILD_NEW_RTL_SDR_SOURCE)
   add_subdirectory("source_modules/new_rtlsdr_source")
   endif (OPT_BUILD_NEW_RTL_SDR_SOURCE)
   ```
   

7) Build sdr++ with:
   ```
   cmake --build . --config Releas
   ```

9) On SDRPlusPlus\root_dev\config.json add this line to modules:[];
   ```
   "./build/source_modules/new_rtlsdr_source/Release/new_rtlsdr_source.dll",
   ```

10) Launch sdr++ from the top directory
  ```
    .\build\Release\sdrpp.exe -r root_dev -c
  ```

#### Windows development notice
If you open the project on vscode probably all the lines will be red because vscode cant find the rtlsdr files but it compiles fine.


# Building on Other distributions
```
* Build https://github.com/Sultan-papagani/rtlsdr-olddab
* on "new_rtlsdr_source\CMakeLists.txt" set cmake paths wherever you put the old-dab and compile
* on SDRPlusPlus\CMakeLists.txt add the same lines as windows
* Compile sdr++
* on root_dev dont forget to add the source to modules[] just like windows
```

## About Code
There is bunch of commented code, you can uncomment it and explore rtl-sdr's settings even further, here is the some of the options:
![Screenshot_5](https://github.com/Sultan-papagani/sdrpp_rtlsdr_source/assets/69393574/a987de0c-febd-412c-b8f1-9c7b7948c4a1)


## used libraries
```
https://github.com/old-dab/rtlsdr
```

