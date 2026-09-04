# Third-party notices

JenyaDereverb2's C++ online WPE implementation is based on the algorithm and recursive update
formulation published by the NARA-WPE project. No Python or TensorFlow runtime is bundled.

The active neural path embeds the official DPDFNet 48 kHz high-resolution ONNX model from
CEVA DSP / AudioLabs and runs it with ONNX Runtime. DPDFNet is distributed under the
Apache License 2.0. ONNX Runtime is distributed under the MIT License. The unmodified
license and third-party notice files shipped with ONNX Runtime are retained in
`ThirdParty/onnxruntime-osx-universal2-1.23.2/`.

## DPDFNet

Copyright 2025 CEVA, Inc. and contributors. Licensed under the Apache License, Version 2.0.
You may obtain a copy at https://www.apache.org/licenses/LICENSE-2.0

## ONNX Runtime

Copyright (c) Microsoft Corporation. All rights reserved. Licensed under the MIT License;
the complete license text is retained at
`ThirdParty/onnxruntime-osx-universal2-1.23.2/LICENSE`.

## NARA-WPE

MIT License

Copyright (c) 2018 Communications Engineering Group, Paderborn University

Permission is hereby granted, free of charge, to any person obtaining a copy of this
software and associated documentation files (the "Software"), to deal in the Software
without restriction, including without limitation the rights to use, copy, modify,
merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit
persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or
substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
