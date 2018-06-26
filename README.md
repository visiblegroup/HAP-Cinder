# HAP-Cinder
A cinder port of [ofxDSHapVideoPlayer](https://github.com/secondstory/ofxDSHapVideoPlayer) openFrameworks addon. Majority of the port was done by [dave](https://discourse.libcinder.org/u/dave), the last few steps were done in collaboration with [these guys](https://discourse.libcinder.org/t/opengl-extensions-glew-glee-gloader/1195).

HAP-Cinder plays Hap-encoded videos natively on Windows using Direct Show video playback tools.

Hap is a video codec that is decoded on the GPU. Some of its benefits include fast decompression and low CPU usage. It was written by Tom Butterworth for VidVox/VDMX. For further information on Hap, see http://vdmx.vidvox.net/blog/hap.

## Requirements
Before encoding videos using Hap codec, install the Hap codec for Direct Show. An installer is available from [Renderheads website](http://www.renderheads.com/downloads/2015/HapDirectShowCodecSetup.exe).

## Notes (copied from ofxDSHapVideoPlayer Repo)
- HAP-Cinder plays back Hap videos in .avi containers. It will not play back Hap-encoded video files in QuickTime movie containers. Encode Windows .avi files using Adobe Media Encoder, After Effects or other.
- Sample Hap video files are available on http://renderheads.com/product/hap-for-directshow/
- HAP-Cinder now supports snappy compression, but the performance is not as snappy.
- HAP-Cinder was tested with cinder 0.9.0.






