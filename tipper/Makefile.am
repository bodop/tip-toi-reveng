AUTOMAKE_OPTIONS=foreign

BUILT_SOURCES = parser.h
AM_YFLAGS = -d

bin_PROGRAMS=tipdecode tipencode tipplay

tipdecode_SOURCES=tipdecode.c gme.h gme.c

tipencode_SOURCES=tipencode.c token.l parser.y gme.c

tipplay_SOURCES=tipplay.c gme.h gme.c tipplay.h selector.h selector.c

