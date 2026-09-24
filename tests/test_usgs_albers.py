"""Regression coverage for the canonical USGS Albers shatter profile."""

from __future__ import annotations

import copy
import xml.etree.ElementTree as ET
from datetime import datetime

import numpy as np
import pdal
import pyproj
import tiledb

from silvimetric import (
    Attributes,
    Bounds,
    Data,
    Extents,
    Log,
    ShatterConfig,
    Storage,
    StorageConfig,
    shatter,
)
from silvimetric.commands.extract import extract
from silvimetric.commands.shatter import get_data
from silvimetric.resources.config import ExtractConfig
from silvimetric.resources.metrics.grid_metrics import get_grid_metrics
from silvimetric.resources.usgs_albers import (
    USGS_ALBERS_CONUS_BOUNDS,
    USGS_ALBERS_CRS,
    USGS_ALBERS_HORIZONTAL_CRS,
    USGS_ALBERS_TOP_LEFT_X,
    USGS_ALBERS_TOP_LEFT_Y,
)
from silvimetric.cli import cli


RESOLUTION = 20.0
# A compact Oregon window after reprojecting the Autzen fixture to EPSG:5070.
WINDOW_A = Bounds(-2_132_600, 2_646_300, -2_132_300, 2_646_600)
WINDOW_B = Bounds(-2_132_500, 2_646_350, -2_132_200, 2_646_650)


def _profile_config(path, *, xsize=8, ysize=8) -> StorageConfig:
    """Create a deliberately contradictory config to prove profile ownership."""
    return StorageConfig(
        tdb_dir=str(path),
        log=Log('INFO'),
        root=Bounds(1, 2, 3, 4),
        crs='EPSG:3857',
        resolution=RESOLUTION,
        alignment='AlignToCenter',
        attrs=[copy.deepcopy(Attributes['Z'])],
        metrics=[get_grid_metrics()['mean']],
        xsize=xsize,
        ysize=ysize,
        usgs_albers=True,
    )


def test_profile_owns_crs_root_and_pixel_is_area_contract(tmp_path):
    config = _profile_config(tmp_path / 'profile.tdb')

    assert config.crs == pyproj.CRS.from_user_input(USGS_ALBERS_CRS)
    assert config.root == USGS_ALBERS_CONUS_BOUNDS
    assert config.alignment == 'PixelIsArea'
    assert config.root.maxy == USGS_ALBERS_TOP_LEFT_Y
    assert config.root.minx == USGS_ALBERS_TOP_LEFT_X
    assert StorageConfig.from_string(str(config)) == config

    storage = Storage.create(config)
    with tiledb.open(storage.config.tdb_dir, 'r') as array:
        gdal_metadata = array.meta['_gdal']
    root = ET.fromstring(gdal_metadata)
    assert root.findtext("./Metadata/MDI[@key='AREA_OR_POINT']") == 'Area'
    assert root.findtext('./GeoTransform') == (
        '-2493045.0, 20.0, 0.0, 3310005.0, 0.0, -20.0'
    )
    assert 'NAVD88 height' in root.findtext('./SRS')


def test_profile_reprojects_source_and_keeps_origin_based_pixel_indices(
    autzen_filepath, tmp_path
):
    """PDAL output is EPSG:5070 coordinates and indices use the fixed root."""
    config = _profile_config(tmp_path / 'profile.tdb')
    Storage.create(config)

    source = pdal.Reader(autzen_filepath).pipeline()
    source.execute()
    source_array = source.arrays[0]

    data = Data(autzen_filepath, config)
    data.execute()
    output = data.array
    assert len(output) == len(source_array)

    source_crs = Data.get_crs(pdal.Reader(autzen_filepath))
    transformer = pyproj.Transformer.from_crs(
        Data._horizontal_crs(source_crs),
        USGS_ALBERS_HORIZONTAL_CRS,
        always_xy=True,
    )
    expected_x, expected_y = transformer.transform(
        source_array['X'][:128], source_array['Y'][:128]
    )
    np.testing.assert_allclose(output['X'][:128], expected_x, atol=1e-5)
    np.testing.assert_allclose(output['Y'][:128], expected_y, atol=1e-5)
    assert 'EPSG:5070+5703' in data.pipeline.pipeline

    # Pixel-is-area coordinates use a fixed top-left *edge*.  This equation is
    # deliberately independent of the fixture's local output bounds.
    np.testing.assert_allclose(
        output['xi'][:128],
        (output['X'][:128] - USGS_ALBERS_TOP_LEFT_X) / RESOLUTION,
    )
    np.testing.assert_allclose(
        output['yi'][:128],
        (USGS_ALBERS_TOP_LEFT_Y - output['Y'][:128]) / RESOLUTION,
    )
    assert np.all(np.floor(output['xi'][:128]) >= 0)
    assert np.all(np.floor(output['yi'][:128]) >= 0)


def test_profile_shatter_uses_floor_for_pixel_area_rows(autzen_filepath, tmp_path):
    """A shatter read must not move each Albers point one raster row south."""
    config = _profile_config(tmp_path / 'profile.tdb')
    storage = Storage.create(config)
    extent = Extents(
        copy.deepcopy(WINDOW_A), config.resolution, config.alignment, config.root
    )

    points = get_data(extent, autzen_filepath, storage, reader_collar=20)
    source = Data(
        autzen_filepath, storage.config, bounds=extent.bounds, reader_collar=20
    )
    source.execute()
    raw = source.pipeline.get_dataframe(0)
    raw = raw.loc[
        (raw.Y < extent.bounds.maxy)
        & (raw.Y >= extent.bounds.miny)
        & (raw.X >= extent.bounds.minx)
        & (raw.X < extent.bounds.maxx)
    ]
    assert len(points) == len(raw) > 0
    # COPC may emit the same points in a different order on two reads.
    actual, actual_counts = np.unique(
        points[['xi', 'yi']].to_numpy(dtype=np.int32),
        axis=0,
        return_counts=True,
    )
    expected, expected_counts = np.unique(
        np.column_stack((np.floor(raw['xi']), np.floor(raw['yi']))).astype(
            np.int32
        ),
        axis=0,
        return_counts=True,
    )
    np.testing.assert_array_equal(actual, expected)
    np.testing.assert_array_equal(actual_counts, expected_counts)


def test_profile_falls_back_when_pyproj_returns_nonfinite_bounds(monkeypatch):
    """A Dask worker must never hand PDAL ``[inf, inf, inf, inf]`` bounds.

    Linux/aarch64 workers have demonstrated a PROJ initialization-order issue
    where ``transform_bounds`` returns infinities instead of raising.  The
    fallback uses GDAL/OSR and retains the required traditional X/Y ordering.
    """

    class NonfiniteTransformer:
        def transform_bounds(self, *args, **kwargs):
            return (np.inf, np.inf, np.inf, np.inf)

    monkeypatch.setattr(
        pyproj.Transformer,
        'from_crs',
        lambda *args, **kwargs: NonfiniteTransformer(),
    )
    transformed = Data._transform_bounds(
        Bounds(19_735, 2_660_745, 20_295, 2_661_045),
        USGS_ALBERS_HORIZONTAL_CRS,
        pyproj.CRS.from_epsg(3857),
    )
    assert np.isfinite(transformed.get()).all()
    assert transformed.minx < -10_657_000
    assert transformed.maxx > -10_658_000
    assert transformed.miny < transformed.maxy


def test_profile_cross_database_selection_has_identical_pixel_space(
    autzen_filepath, tmp_path
):
    """The same EPSG:5070 window has the same grid domain and raster values."""
    first = Storage.create(_profile_config(tmp_path / 'first.tdb'))
    second = Storage.create(_profile_config(tmp_path / 'second.tdb'))

    for storage, window in ((first, WINDOW_A), (second, WINDOW_B)):
        config = ShatterConfig(
            tdb_dir=storage.config.tdb_dir,
            filename=autzen_filepath,
            date=datetime(2020, 1, 1),
            bounds=copy.deepcopy(window),
            tile_size=64,
            usgs_albers=True,
            log=Log('INFO'),
        )
        assert ShatterConfig.from_string(str(config)) == config
        shatter(config)
        assert Storage.from_db(storage.config.tdb_dir).get_history()[-1][
            'usgs_albers'
        ]

    # This shared target rectangle is expressed once in the canonical CRS.
    # Its TileDB index domain is identical regardless of each database's
    # independently selected input footprint.
    shared = Bounds(-2_132_480, 2_646_380, -2_132_340, 2_646_560)
    first_extent = Extents.from_sub(first, copy.deepcopy(shared))
    second_extent = Extents.from_sub(second, copy.deepcopy(shared))
    assert first.config.root == second.config.root == USGS_ALBERS_CONUS_BOUNDS
    assert first_extent.domain == second_extent.domain

    # Extracting that same pixel-space selection produces matching
    # pixel-is-area GeoTIFFs.  ``mean`` needs a valid point population in the
    # overlap; the compact windows above share this interior.
    output_a = tmp_path / 'extract-a'
    output_b = tmp_path / 'extract-b'
    extract(
        ExtractConfig(
            tdb_dir=first.config.tdb_dir,
            out_dir=str(output_a),
            bounds=copy.deepcopy(shared),
            log=Log('INFO'),
        )
    )
    extract(
        ExtractConfig(
            tdb_dir=second.config.tdb_dir,
            out_dir=str(output_b),
            bounds=copy.deepcopy(shared),
            log=Log('INFO'),
        )
    )

    from osgeo import gdal

    raster_a = gdal.Open(str(output_a / 'm_Z_mean.tif'))
    raster_b = gdal.Open(str(output_b / 'm_Z_mean.tif'))
    assert raster_a.GetMetadataItem('AREA_OR_POINT') == 'Area'
    assert raster_b.GetMetadataItem('AREA_OR_POINT') == 'Area'
    assert raster_a.GetGeoTransform() == raster_b.GetGeoTransform()
    np.testing.assert_allclose(
        raster_a.GetRasterBand(1).ReadAsArray(),
        raster_b.GetRasterBand(1).ReadAsArray(),
        equal_nan=True,
    )


def test_profile_shatter_flag_must_match_database(autzen_filepath, tmp_path):
    storage = Storage.create(_profile_config(tmp_path / 'profile.tdb'))
    config = ShatterConfig(
        tdb_dir=storage.config.tdb_dir,
        filename=autzen_filepath,
        date=datetime(2020, 1, 1),
        bounds=copy.deepcopy(WINDOW_A),
        tile_size=64,
        log=Log('INFO'),
    )

    try:
        shatter(config)
    except ValueError as error:
        assert '--usgs_albers' in str(error)
    else:
        raise AssertionError('profile mismatch unexpectedly wrote the database')


def test_cli_defaults_to_profile_without_user_bounds_or_crs(runner, tmp_path):
    database = tmp_path / 'profile.tdb'
    result = runner.invoke(
        cli.cli,
        [
            '-d',
            str(database),
            'initialize',
            '--resolution',
            '20',
            '--xsize',
            '8',
            '--ysize',
            '8',
        ],
        catch_exceptions=False,
    )
    assert result.exit_code == 0, result.output
    config = Storage.from_db(str(database)).config
    assert config.usgs_albers
    assert config.root == USGS_ALBERS_CONUS_BOUNDS


def test_cli_can_explicitly_opt_out_of_albers(runner, tmp_path):
    database = tmp_path / 'source-crs.tdb'
    result = runner.invoke(
        cli.cli,
        [
            '-d', str(database), 'initialize', '--no-usgs-albers',
            '--bounds', '[0, 0, 100, 100]', '--crs', 'EPSG:3857',
            '--resolution', '20', '--xsize', '5', '--ysize', '5',
        ],
        catch_exceptions=False,
    )
    assert result.exit_code == 0, result.output
    assert not Storage.from_db(str(database)).config.usgs_albers


def test_cli_shatter_inherits_profile_without_flag(
    runner, autzen_filepath, tmp_path
):
    database = tmp_path / 'profile.tdb'
    Storage.create(_profile_config(database))
    result = runner.invoke(
        cli.cli,
        [
            '-d',
            str(database),
            '--scheduler',
            'single-threaded',
            'shatter',
            autzen_filepath,
            '--bounds',
            str(WINDOW_A),
            '--date',
            '2020-01-01',
            '--tilesize',
            '64',
        ],
        catch_exceptions=False,
    )
    assert result.exit_code == 0, result.output
    assert Storage.from_db(str(database)).get_history()[-1]['usgs_albers']
