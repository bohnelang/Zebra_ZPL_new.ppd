# Zebra_ZPL_EN_DE.ppd
CUPS PPD file for Zebra label printer GX GK 420 430 and more with ZPL language interpreter

Zebra label printer prints charactes that are rastered to image that is print by label printer.

[https://github.com/apple/cups/blob/master/filter/rastertolabel.c](https://github.com/apple/cups/blob/master/filter/rastertolabel.c)

```
+------------+       +------------+       +---------------+       +---------------+
| Text       | --->  | raster2img | --->  | convert2ZPL   | --->  | label printer |
+------------+       +------------+       +---------------+       +---------------+
                                           rastertolabel.c
```

The PPD file was improved and I did some bug fixes. Furthermore I did a translation to German language (and remove other language than English). 
For our work this is ok. Feel free to use it, if you like.

Tested with:
* GK420t
* GX420t

The original file is zabra.ppd and can be found at: https://opensource.apple.com/source/cups/cups-136/cups/ppd/zebra.ppd.auto.html

The Zeba website contains useful information:
* [Identify-type-of-media](https://supportcommunity.zebra.com/s/article/Identify-type-of-media?language=en_U)

Information about ZPL language can be found here: [ZPL-Command-Information-and-Details](https://supportcommunity.zebra.com/s/article/ZPL-Command-Information-and-DetailsV2?language=de) and a [programming book](https://support.zebra.com/cpws/docs/zpl/zpl-zbi2-pm-en.pdf)


Useful font sizes:
```
lpr -o cpi=13 -o lpi=5 -o fit-to-page -P <printer name>
lpr -o cpi=16 -o lpi=8 -o fit-to-page -P <printer name>
```

Usevul inks:
* [ZPL online emulator](http://labelary.com/viewer.html)
