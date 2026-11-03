# Receiver-plane shadow depth correction

Source: https://github.com/walbourn/directx-sdk-samples-reworked/tree/1ad8f0f6a3e4d9be7e54ca52640ac12b6565ab0c

Adapted from `CascadedShadowMaps11/RenderCascadeScene.hlsl`,
`CalculateRightAndUpTexelDepthDeltas` and `CalculatePCFPercentLit`.

The engine projects world-position derivatives through each cascade before solving
its receiver-plane depth gradient. PCF compares depth at the actual point-sampled
texel centre; singular derivatives fall back to the configured constant bias.
World-position derivatives are evaluated before cascade selection and reused when
computing each cascade's gradient and blending interval.

## License

```
                               The MIT License (MIT)

Copyright (c) 2004-2022 Microsoft Corp

Permission is hereby granted, free of charge, to any person obtaining a copy of this
software and associated documentation files (the "Software"), to deal in the Software
without restriction, including without limitation the rights to use, copy, modify,
merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to the following
conditions:

The above copyright notice and this permission notice shall be included in all copies
or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```
