# Third-party software

Vocal Ink is licensed under the Apache License 2.0 (see `LICENSE`). It is built
with, or downloads at the user's request, the following third-party software.

## Bundled with the app

| Component | License | Notes |
|---|---|---|
| [Qt 6](https://www.qt.io/) (Core, Gui, Widgets, Qml, Quick, Quick Controls, Quick Shapes, Network, Multimedia, TextToSpeech, WebSockets, Concurrent) | LGPL-3.0 | Dynamically linked. You may replace the Qt libraries shipped with Vocal Ink with your own build. Source: https://download.qt.io/official_releases/qt/ |
| [FFmpeg](https://ffmpeg.org/) (shipped by Qt Multimedia) | LGPL-2.1+ | Dynamically linked, as distributed by the Qt project. |
| [whisper.cpp / ggml](https://github.com/ggml-org/whisper.cpp) v1.9.4 | MIT | Statically linked on-device speech recognition. Copyright (c) 2023-2026 The ggml authors. |
| [QHotkey](https://github.com/Skycoder42/QHotkey) 1.5.0 | BSD-3-Clause | System-wide shortcuts. Copyright (c) 2016 Felix Barz. |
| [QtKeychain](https://github.com/frankosterfeld/qtkeychain) 0.17.0 | BSD-3-Clause | Secure storage of API keys. Copyright (c) 2011-2026 Frank Osterfeld and contributors. |
| [Bricolage Grotesque](https://github.com/ateliertriay/bricolage) | OFL-1.1 | Display and stage type. Static instances in `resources/fonts` (see `LICENSES.md` there). |
| [Atkinson Hyperlegible Next & Mono](https://github.com/googlefonts/atkinson-hyperlegible-next) | OFL-1.1 | Interface text and keycaps. © Braille Institute of America. |
| [Lexend](https://github.com/googlefonts/lexend) | OFL-1.1 | Optional reading font. |
| [OpenDyslexic](https://github.com/antijingoist/opendyslexic) | OFL-1.1 | Optional reading font, unmodified. |
| [Lucide icons](https://lucide.dev) | ISC | Interface icons, converted to path data in `src/qml/Icons.js`. |
| [wordfreq](https://github.com/rspeer/wordfreq) word list | CC BY-SA 4.0 | Base vocabulary for word prediction in `resources/predict/`. By Robyn Speer; filtered and reformatted. |

## Vocal Ink's virtual microphone drivers

See [docs/VIRTUAL_AUDIO.md](docs/VIRTUAL_AUDIO.md). License texts are at the end of this file.

| Component | License | Notes |
|---|---|---|
| [libASPL](https://github.com/gavv/libASPL) v3.1.2 | MIT | macOS: statically linked into the `VocalInkVirtualMic.driver` HAL plug-in. Copyright (c) Victor Gaydov and contributors. Includes parts of Apple's audio server plug-in examples (MIT and "Apple MIT" licenses, Copyright (c) 2012, 2020 Apple Inc.). |
| [SimpleAudioSample](https://github.com/microsoft/Windows-driver-samples/tree/main/audio/simpleaudiosample) from Microsoft's Windows-driver-samples | MS-PL | Windows: the `VocalInkAudio.sys` driver is derived from it. Copyright (c) Microsoft Corporation. Its source, with our changes, is in `driver/windows` and stays under the MS-PL. |
| [nefcon](https://github.com/nefarius/nefcon) v1.21.0 (`nefconw.exe`) | MIT | Windows: shipped next to the driver to install and remove it. Copyright (c) 2022-2026 Nefarius Software Solutions e.U. |

## Downloaded on request (not bundled)

| Component | License | Notes |
|---|---|---|
| [Piper](https://github.com/rhasspy/piper) 2023.11.14-2 runtime | MIT | Local neural text-to-speech engine, downloaded from GitHub releases when you choose to install it. Includes espeak-ng data (GPL-3.0) and ONNX Runtime (MIT) as distributed by the Piper project. |
| [Piper voices](https://huggingface.co/rhasspy/piper-voices) | Varies per voice | Each voice has its own license, listed in its `MODEL_CARD` on Hugging Face. |
| [Whisper models](https://huggingface.co/ggerganov/whisper.cpp) (ggml format) | MIT | OpenAI Whisper weights converted for whisper.cpp. |

## Optional external programs

Vocal Ink can use these if they are installed on your system; they are not
distributed with it:

- [eSpeak NG](https://github.com/espeak-ng/espeak-ng) (GPL-3.0), run as a separate process.
- [VB-CABLE](https://vb-audio.com/Cable/) (donationware) or [BlackHole](https://github.com/ExistentialAudio/BlackHole) (GPL-3.0), as fallbacks to Vocal Ink's own virtual microphone for routing audio to other apps.

## Cloud services

Microsoft Azure Speech, ElevenLabs, Fish Audio and OpenAI are used only when you
add your own API key. Text you choose to speak with a cloud voice (and audio you
choose to transcribe with a cloud recognizer) is sent to that provider under
their terms of service.

## License texts

### libASPL (MIT)

```
The MIT License (MIT)

Copyright (c) Victor Gaydov and contributors.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

Parts of libASPL come from Apple's examples:

```
Copyright © 2020 Apple Inc.

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

```
Disclaimer: IMPORTANT:  This Apple software is supplied to you by Apple
Inc. ("Apple") in consideration of your agreement to the following
terms, and your use, installation, modification or redistribution of
this Apple software constitutes acceptance of these terms.  If you do
not agree with these terms, please do not use, install, modify or
redistribute this Apple software.

In consideration of your agreement to abide by the following terms, and
subject to these terms, Apple grants you a personal, non-exclusive
license, under Apple's copyrights in this original Apple software (the
"Apple Software"), to use, reproduce, modify and redistribute the Apple
Software, with or without modifications, in source and/or binary forms;
provided that if you redistribute the Apple Software in its entirety and
without modifications, you must retain this notice and the following
text and disclaimers in all such redistributions of the Apple Software.
Neither the name, trademarks, service marks or logos of Apple Inc. may
be used to endorse or promote products derived from the Apple Software
without specific prior written permission from Apple.  Except as
expressly stated in this notice, no other rights or licenses, express or
implied, are granted by Apple herein, including but not limited to any
patent rights that may be infringed by your derivative works or by other
works in which the Apple Software may be incorporated.

The Apple Software is provided by Apple on an "AS IS" basis.  APPLE
MAKES NO WARRANTIES, EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION
THE IMPLIED WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY AND FITNESS
FOR A PARTICULAR PURPOSE, REGARDING THE APPLE SOFTWARE OR ITS USE AND
OPERATION ALONE OR IN COMBINATION WITH YOUR PRODUCTS.

IN NO EVENT SHALL APPLE BE LIABLE FOR ANY SPECIAL, INDIRECT, INCIDENTAL
OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) ARISING IN ANY WAY OUT OF THE USE, REPRODUCTION,
MODIFICATION AND/OR DISTRIBUTION OF THE APPLE SOFTWARE, HOWEVER CAUSED
AND WHETHER UNDER THEORY OF CONTRACT, TORT (INCLUDING NEGLIGENCE),
STRICT LIABILITY OR OTHERWISE, EVEN IF APPLE HAS BEEN ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

Copyright (C) 2012 Apple Inc. All Rights Reserved.
```

### nefcon (MIT)

```
MIT License

Copyright (c) 2022-2026 Nefarius Software Solutions e.U.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

### SimpleAudioSample (Microsoft Public License)

```
The Microsoft Public License (MS-PL)
Copyright (c) 2015 Microsoft

This license governs use of the accompanying software. If you use the software, you
 accept this license. If you do not accept the license, do not use the software.

1. Definitions
 The terms "reproduce," "reproduction," "derivative works," and "distribution" have the
 same meaning here as under U.S. copyright law.
 A "contribution" is the original software, or any additions or changes to the software.
 A "contributor" is any person that distributes its contribution under this license.
 "Licensed patents" are a contributor's patent claims that read directly on its contribution.

2. Grant of Rights
 (A) Copyright Grant- Subject to the terms of this license, including the license conditions and limitations in section 3, each contributor grants you a non-exclusive, worldwide, royalty-free copyright license to reproduce its contribution, prepare derivative works of its contribution, and distribute its contribution or any derivative works that you create.
 (B) Patent Grant- Subject to the terms of this license, including the license conditions and limitations in section 3, each contributor grants you a non-exclusive, worldwide, royalty-free license under its licensed patents to make, have made, use, sell, offer for sale, import, and/or otherwise dispose of its contribution in the software or derivative works of the contribution in the software.

3. Conditions and Limitations
 (A) No Trademark License- This license does not grant you rights to use any contributors' name, logo, or trademarks.
 (B) If you bring a patent claim against any contributor over patents that you claim are infringed by the software, your patent license from such contributor to the software ends automatically.
 (C) If you distribute any portion of the software, you must retain all copyright, patent, trademark, and attribution notices that are present in the software.
 (D) If you distribute any portion of the software in source code form, you may do so only under this license by including a complete copy of this license with your distribution. If you distribute any portion of the software in compiled or object code form, you may only do so under a license that complies with this license.
 (E) The software is licensed "as-is." You bear the risk of using it. The contributors give no express warranties, guarantees or conditions. You may have additional consumer rights under your local laws which this license cannot change. To the extent permitted under your local laws, the contributors exclude the implied warranties of merchantability, fitness for a particular purpose and non-infringement.
```
