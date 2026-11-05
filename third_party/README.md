# Vendored dependencies

The project builds offline using these source releases:

- picojson v1.3.0: https://raw.githubusercontent.com/kazuho/picojson/v1.3.0/picojson.h
- libwebp 1.6.0: https://storage.googleapis.com/downloads.webmproject.org/releases/webp/libwebp-1.6.0.tar.gz

`build.ps1` compiles the codec to a static library. Only the decoder is referenced by the desktop EXE; test tooling uses the encoder to generate original PNG v1 and WebP v2 fixtures. WebP support does not depend on an installed Windows WebP codec.

Original license and patent notices are preserved in the sources and copied to `out/licenses` for distribution.
