(initialize)=

# initialize

```{only} html
Initialize constructs a SilviMetric database
```

```{Index} initialize
```

The `initialize` subcommand constructs the basic {{ TileDB }} instance to host the
SilviMetric data. It can be either a local filesystem path or a {{ S3 }} URI (eg.
`s3://silvimetric/mydata`).

## Synopsis

```
Usage: silvimetric [OPTIONS] initialize [OPTIONS]

Initialize silvimetrics DATABASE

Options:
--bounds BOUNDS         Root bounds that encapsulates all data
--crs CRS               Coordinate system of data
-a, --attributes ATTRS  List of attributes to include in Database
-m, --metrics METRICS   List of metrics to include in Database
--resolution FLOAT      Summary pixel resolution
--help                  Show this message and exit.
```

`--bounds` and `--crs` are required for a normal database.  The
[`--usgs_albers`](../usgs_albers.md) profile is the exception: it fixes both
values to the canonical CONUS grid and uses pixel-is-area alignment.

## Example

```
silvimetric --database test.tdb initialize --crs "EPSG:3857" \
    --bounds '[300, 300, 600, 600]'
```
