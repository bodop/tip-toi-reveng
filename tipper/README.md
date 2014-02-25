Decode, encode and play tiptoi GME files.
-----------------------------------------

This package contains three programs for decoding, encoding and playing tiptoi files.

`tipdecode` and `tipencode` use a grammar that is similar to the yaml
files of the parent project. `tipplay` uses `ogg123` and `aplay` to play the
sound files. It also may starts a kind of HTTP server that allows the
maps stored in the nearby directory to execute the GME scripts.
 
### Installation

Just run

    aclocal
    automake -a
    autoconf
    make

and hope that all prerequisites are available.