# Zebra_ZPL_EN_DE.ppd
CUPS PPD file for Zebra label printer GX GK 420 430 and more with ZPL language interpreter

Zebra label printer prints charactes that a rasterd to image that is print by label printer.

https://opensource.apple.com/source/cups/cups-450/cups/filter/rastertolabel.c.auto.html

```
+------------+       +------------+       +---------------+       +---------------+
| Text       | -->   | raster2img | --->  | convert2ZPL   | --->  | label printer |
+------------+       +------------+       +---------------+       +---------------+
```

The PPD file was improved and I did some bug fixes. Furthermore I did a translation to German language (and remove othe language). 
For our work this is ok. Feel free to use it, if you like.

Tested with:
* GK420t
* GX420t

The original file is zabra.ppd an can be found at: https://opensource.apple.com/source/cups/cups-136/cups/ppd/zebra.ppd.auto.html

The Zeba website contains useful information:
* [Identify-type-of-media](https://supportcommunity.zebra.com/s/article/Identify-type-of-media?language=en_U)

Information about ZPL language can be found here: [ZPL-Command-Information-and-Details](https://supportcommunity.zebra.com/s/article/ZPL-Command-Information-and-DetailsV2?language=de) and a [programming book](https://support.zebra.com/cpws/docs/zpl/zpl-zbi2-pm-en.pdf)

