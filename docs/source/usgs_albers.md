(usgs-albers)=

# Canonical USGS Albers grid

Use `--usgs_albers` when independently built databases must share one stable
pixel coordinate system across CONUS.  The profile is designed for comparisons
in row/column space: pixel `(x, y)` identifies the same 20 m (or other chosen
resolution) area in every profile database.

The profile fixes all of the following storage choices:

- CRS: `EPSG:5070+5703` (`NAD83 / Conus Albers + NAVD88 height`);
- pixel convention: pixel-is-area;
- upper-left outer pixel edge: `(-2493045, 3310005)` metres;
- complete CONUS grid outer bounds: `[-2493045, 177285, 2342655, 3310005]`.

## Pixel-space origin

The coordinate `(-2493045, 3310005)` is the **zero-based pixel-space
origin**: it is the upper-left *outer edge* of pixel `(column=0, row=0)`, not
that pixel's centre.  At resolution `r` metres, a point with Albers coordinate
`(X, Y)` belongs to:

```text
column = floor((X - -2493045) / r)
row    = floor((3310005 - Y) / r)
```

Thus columns increase eastward and rows increase southward.  Pixel `(0, 0)`
has outer bounds `[-2493045, 3310005-r, -2493045+r, 3310005]`; its centre is
`(-2493045 + r/2, 3310005 - r/2)`.  This is also the GDAL geotransform origin,
with pixel size `(r, -r)`.  Use these same integer row/column values, or the
same target bounds, to compare any two databases built with this profile.

The full common root is stored in each database schema, but shatter only plans
the TileDB tiles intersecting the supplied source footprint.  Initializing a
small collection therefore does not enumerate or materialize empty CONUS
pixels.

## Create and shatter a profile database

```shell
silvimetric -d conus.tdb initialize \
  --usgs_albers --resolution 20 --xsize 256 --ysize 256 \
  -m grid_metrics

silvimetric -d conus.tdb shatter SOURCE.copc.laz \
  --usgs_albers --date 2021-01-01 --tilesize 65536
```

`initialize --usgs_albers` intentionally does not require `--bounds` or
`--crs`: it owns those two settings.  `shatter --usgs_albers` is required for
every write to that database, and is rejected for ordinary databases.  This
prevents a source-coordinate write from corrupting a canonical array.

In profile mode, `shatter --bounds` is an `EPSG:5070` **target** rectangle.
Silvimetric transforms it into the reader's native CRS only to bound the
COPC/EPT/tindex query, reprojects all points with PDAL to `EPSG:5070+5703`,
and writes target-grid indices.  The final crop is in target coordinates, so
the same bounds can be reused verbatim for any profile database:

```shell
silvimetric -d first.tdb extract -o first \
  --bounds '[-2132605,2646285,-2132285,2646605]'
silvimetric -d second.tdb extract -o second \
  --bounds '[-2132605,2646285,-2132285,2646605]'
```

The TileDB GDAL PAM document and extracted GeoTIFFs describe a north-up
pixel-is-area grid: their geotransform begins at the fixed outer edge and has
negative Y pixel size.  This means GDAL, rasterio, and xarray selections have
the same row/column interpretation as the TileDB array.
